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

#include <iostream>

/* This is a basic demo Bot that places a passive buy order on Binance. The
   order is positioned far from the bid/ask to prevent execution.  After 20
   seconds the order is canceled. This example shows how orders are priced,
   sized, created, sent and then later managed.
*/
class OneOrderDemoBot : public apex::Bot
{
public:

  OneOrderDemoBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("OneOrderDemoBot", strategy, instrument) {}

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
    if (!market_data_ok() || !has_fx_rate()) {
      LOG_WARN(ticker() << ": waiting for market data");
      return;
    }

    if (is_stopping())
      return;

    // desired value of the order USD
    auto order_usd = 25.0;

    // choose price 1% away from last trade, so that it doesn't execute
    double price = round_price_passive(last_price() * 0.99, apex::Side::buy);

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

int main()
{
  try {
    // create core engine, configured for paper or live trading
    apex::Logger::instance().set_level(apex::Logger::info);
    auto core = apex::Core::create(apex::RunMode::paper);

    // ----------------------------------------------------------------------
    // CONFIGURE CORE SERVICES
    // ----------------------------------------------------------------------

    // Setup order gateway and price feeds to Binance & Binance USD Futures.

    apex::FeedConfig feed_config;
    auto router_service = core->order_router_service();

    // add a Binance USD-Futures line hander order-router
    apex::OrderRouterConfig binance_config;
    binance_config.api_key_file = apex::expand("~/.secrets/binance_key.json");
    router_service->add_binance_usdfut(binance_config);

    // add a Binance USD-Futures feed hanlder
    feed_config.type = "BinanceUsdFut";
    core->market_data_service()->add_feed(feed_config, {"binance_usdfut"});

    // add a Binance Spot line hander order-router
    router_service->add_binance_spot(binance_config);

    // add a Binance Spot feed hanlder
    feed_config.type = "Binance";
    core->market_data_service()->add_feed(feed_config, {"binance"});

    // ----------------------------------------------------------------------
    // SINGLE ORDER STRATEGY
    // ----------------------------------------------------------------------

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(core.get(), "DEM01");

    // add a bot, which is responsible for trading a single name
    strategy.create_bot<OneOrderDemoBot>(apex::InstrumentQuery(
                                           "DOGEUSDT",
                                           apex::ExchangeId::binance));

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // ----------------------------------------------------------------------
    // RUN
    // ----------------------------------------------------------------------

    // run until user presses control-c
    core->run();
  }
  catch (std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    return 1;
  }
}
