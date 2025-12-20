/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#include <apex/core/Bot.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/util/AllocTracker.hpp>

#include <iostream>

struct StrategyConfig
{
  std::string id;
  std::string exchange;
  std::vector<std::string> universe;

  static auto schema() {
    FIELD_DEF_INIT(StrategyConfig);
    FIELD_DEF_REQUIRED(id);
    FIELD_DEF_REQUIRED(exchange);
    FIELD_DEF_REQUIRED(universe);
    FIELD_DEF_RETURN();
  }
};


struct LatencyExampleBotConfig
{
  StrategyConfig strategy;;
  apex::SerivcesConfig services;

  static auto schema() {
    FIELD_DEF_INIT(LatencyExampleBotConfig);
    FIELD_DEF_OPTIONAL(strategy, StrategyConfig{});
    FIELD_DEF_OPTIONAL(services, apex::SerivcesConfig{});
    FIELD_DEF_RETURN();
  }

  // non config items
  std::string filename;
};


/* This is an example used only for latency experiments.  It is not an actual
 * trading bot - do not use against live markets. */
class LatencyExampleBot : public apex::Bot
{
public:

  bool enable_send = false;

  LatencyExampleBot(apex::Strategy* strategy,
                    const apex::Instrument& instrument)
    : apex::Bot("LatencyExampleBot", strategy, instrument)
       {}

  void on_order_live(apex::Order& order) {
    LOG_INFO("order live: " << order.order_id());
  }

  void on_timer() override
  {
    using namespace std::chrono_literals;

    // Every 1 second run the Bot logic, which is just an order if we already
    // have not, but if we have sent one, then cancel it.

    if (!_order) {
      // We have not created and order, so create and send
      _create_and_send_order();
      return;
    }

    if (_order->is_rejected()) {
      // Our order was rejected - so lets delete the Order instance, so that we
      // can try again later.
      if (_order->duration_since_sent() > 5s)
        _order.reset();
      return;
    }

    if (!_order->is_closed_or_canceling()) {
      // The order is still 'live', so here we will manage it.  Our only
      // management logic is to cancel the order if it's been alive for too
      // long.
      if ((_order->duration_live() > 20s) &&
          (_order->cancel_reject_count() < 3)) {
        _order->cancel();
      }
      return;
    }
  }

private:

  void _create_and_send_order()
  {
    if (!enable_send)
      return;

    if (!market_data_ok() || !has_fx_rate()) {
      LOG_WARN(ticker() << ": waiting for market data");
      return;
    }

    if (is_stopping())
      return;

    // desired value of the order USD
    auto order_usd = 25.0;

    // choose price far away from last trade, so that it doesn't execute
    double price = round_price_passive(last_price() * 0.95, apex::Side::buy);

    // don't send order if calculated order is zero
    if (apex::dbl_is_zero(price)) {
      LOG_WARN("cannot send, calculated order price is " << price);
      return;
    }

    // size the order quantity, based on target price and value
    double qty = round_size(order_usd / (price * fx_rate()));

    // don't send order if calculated order qty is zero
    if (apex::dbl_is_zero(qty)) {
      LOG_WARN("cannot send, calculated order qty is " << qty);
      return;
    }

    // construct an order object (this does not cause it to be sent)
    _order = create_order(apex::Side::buy, qty, price, apex::TimeInForce::gtc);

    // send order to the exchange (this is an asynchronous operation)
    _order->send();
  }

  std::shared_ptr<apex::Order> _order;
};


template <typename T>
T load_config_file(const std::string& filename)
{
  auto raw_data =  apex::read_file(filename);

  try {
    // parse to JSON object
    json raw_config = json::parse(raw_data,
                                  /* callback */ nullptr,
                                  /* allow exceptions */ true,
                                  /* ignore_comments */ true);

    // parse to parameters object
    apex::ConfigParser<T> parser;
    parser.parse(raw_config);
    return parser.result;
  }
  catch (json::parse_error& e)
  {
    throw apex::ConfigError(apex::concat("error parsing config file '",
                                         filename,
                                         "' json parse error: ",
                                         e.what()));
  }
}


LatencyExampleBotConfig parse_args(int argc, char** argv)
{
  std::string filename;

  for (int i = 1; i < argc; i++) {
    if (std::string_view(argv[i]) == "--config") {
      i++;
      if (i < argc)
        filename = argv[i];
      else
        throw std::runtime_error("missing argument to --config");
    }
  }
  if (filename.empty())
    throw std::runtime_error("provide config file, using --config option");

  auto config = load_config_file<LatencyExampleBotConfig>(filename);
  config.filename = std::move(filename);
  return config;

}


std::shared_ptr<std::string> sp;

int do_alloc(int r =1) {
  for (int i = 0; i < r; i++)
    sp = std::make_shared<std::string>("hh");
  return sp->length();
}


int main(int argc, char** argv)
{
  // return test0();

  try {
    auto config = parse_args(argc, argv);

    // ----------------------------------------------------------------------
    // CONFIGURE CORE SERVICES
    // ----------------------------------------------------------------------

    apex::Logger::instance().set_level(apex::Logger::info);
    auto services = apex::Services::create(config.services);

    LOG_INFO("application configuration file '" << config.filename << "'");

    apex::FeedConfig feed_config;

    // add a Binance USD-Futures feed hanlder
    feed_config.type = "BinanceUsdFut";
    services->market_data_service()->add_feed(feed_config, {"binance_usdfut"});

    // add a Binance Spot feed hanlder
    feed_config.type = "Binance";
    services->market_data_service()->add_feed(feed_config, {"binance"});

    // ----------------------------------------------------------------------
    // STRATEGY
    // ----------------------------------------------------------------------

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(services.get(), config.strategy.id);

    // add a bots, one for each name we will trde
    auto exchange = apex::to_exchange_id(config.strategy.exchange);
    if (config.strategy.universe.empty()) {
      throw std::runtime_error("strategy universe can not be empty");
    }
    for (auto & ticker : config.strategy.universe) {
      strategy.create_bot<LatencyExampleBot>(apex::InstrumentQuery(
                                               ticker,
                                               exchange));
    }

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // ----------------------------------------------------------------------
    // RUN
    // ----------------------------------------------------------------------

    // run until user presses control-c
    services->run();
  }
  catch (std::exception& e) {
    std::cout << "error: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    return 1;
  }
}
