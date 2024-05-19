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
#include <apex/core/GatewayService.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/gx/GxServer.hpp>


class SimpleMarketMakerBot : public apex::Bot
{

  struct Config {
    double order_usd = 10;

    // When placing an order we choose a price that is based on a distance from
    // the best limit price.
    double entry_price_distance_bps = 20;

    double price_threshold_bps = 10;
  };
  const Config params;

public:

  SimpleMarketMakerBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("SMM", strategy, instrument) {}

  void on_timer() override
  {
    // Every 1 second run the Bot logic, which is just an order if we already
    // have not, but if we have sent one, then cancel it.
    if (_order)
      _manage_existing_order();
    else
      _create_and_send_order();
  }

  virtual void on_order_fill(apex::Order&) {
    auto pos = this->position();
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

    double price = round_price_passive(last_price() * (1.0 - params.entry_price_distance_bps/10000), apex::Side::buy);

    // determine the market price lower & upper bounds that will prompt a
    // replace of the order
    _replace_price.lower = last_price() * (1.0 - params.price_threshold_bps/10000);
    _replace_price.upper = last_price() * (1.0 + params.price_threshold_bps/10000);

    // size the order quantity, based on target price and value
    double qty = round_size(params.order_usd / (price * fx_rate()));

    // don't send order if calculated order qty is zero
    if (qty == 0)
      return;

    // construct an order object (this does not cause it to be sent)
    _order = create_order(apex::Side::buy, qty, price, apex::TimeInForce::gtc);

    // send order to the exchange (this is an asynchronous operation)
    _order->send();
  }

  void _manage_existing_order()
  {

    if (!_order->is_closed_or_canceling()) {

      if ((last_price() > _replace_price.upper) || last_price() < _replace_price.lower) {
        // LOG_INFO("last price out of range, cancelling order");
        _order->cancel();
      }

      // The order is still 'live', so here we will manage it.  Our only
      // management logic is to cancel the order if it's been alive for too
      // long.

      if (_order->duration_live() > std::chrono::seconds(20)) {
        //_order->cancel();
      }
    }
    else {
      if (_order->is_closed()) {
        _order.reset();
      }
    }
  }

  std::shared_ptr<apex::Order> _order;

  struct PriceBounds {
    double lower;
    double upper;
  };
  PriceBounds _replace_price;
};

int main()
{
  try {
    // This demo can run in either backtest, paper, or live run-mode.  Use this
    // control to decide.
    // THIS
    auto run_mode = apex::RunMode::paper;

    // create core engine, configured for paper or live trading
    std::unique_ptr<apex::Services> services;

    // ----- Framework setup -----

    std::unique_ptr<apex::GxServer> embedded_gateway;

    if (run_mode == apex::RunMode::backtest) {
      // backtest time range
      apex::Time from{"2024-02-01T00:00:00"};
      apex::Time upto{"2024-02-31"};

      // create core engine, configured for backtest
      services = apex::Services::create(run_mode, {from, upto});
    } else {
      services = apex::Services::create(run_mode);

      // create an embedded exchange gateway server
      embedded_gateway = std::make_unique<apex::GxServer>(services->realtime_evloop(),
                                                          run_mode);

      // add Binance to gateway (set user's private API key for live trading)
      apex::BinanceSession::Params params;
      //params.api_key_file = apex::user_home_dir()/".apex/binance_key.json";
      embedded_gateway->add_venue(params);

      // start the gateway, it will listen for connections from the strategy
      embedded_gateway->start();

      // add a route from core engine to the gateway server
      services->gateway_service()->set_default_gateway(embedded_gateway->get_listen_port());
    }

    // ----- Strategy -----

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(services.get(), "DEM02");

    // add a bot, which is responsible for trading a single name
    strategy.create_bot<SimpleMarketMakerBot>(apex::InstrumentQuery(
                                                "BTCUSDT",
                                                apex::ExchangeId::binance));

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // ----- System start -----

    // run until user presses control-c, or backtest time range completed
    services->run();
  }
  catch (std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    return 1;
  }
}
