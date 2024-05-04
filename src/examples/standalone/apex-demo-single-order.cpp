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

/* This is a basic demo Bot that places a passive buy order on Binance. The
   order is positioned far from the bid/ask to prevent execution.  After 10
   seconds the order is canceled, and the process repeated. This example shows
   how orders are priced, sized, created, sent and then later managed.
*/
class OneOrderDemoBot : public apex::Bot
{
public:

  OneOrderDemoBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("OneOrderDemoBot", strategy, instrument) {}

  void on_timer() override
  {
    // Every 1 second run the Bot logic, which is just an order if we already
    // have not, but if we have sent one, then cancel it.
    if (!_order)
      _create_and_send_order();
    else
      _cancel_existing_order();
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
    auto order_usd = 10.0;

    // choose price 1% away from last trade, so that it doesn't execute
    double price = round_price_passive(last_price() * 0.99, apex::Side::buy);

    // size the order quantity, based on target price and value
    double qty = round_size(order_usd / (price * fx_rate()));

    // don't send order if calculated order qty is zero
    if (qty == 0)
      return;

    // construct an order object (this does not cause it to be sent)
    _order = create_order(apex::Side::buy, qty, price, apex::TimeInForce::gtc);

    // send order to the exchange (this is an asynchronous operation)
    _order->send();
  }

  void _cancel_existing_order()
  {
    if (!_order->is_closed_or_canceling()) {
      // The order is still 'live', so here we will manage it.  Our only
      // management logic is to cancel the order if it's been alive for too
      // long.
      if (_order->duration_live() > std::chrono::seconds(20)) {
        _order->cancel();
      }
    }
  }

  std::shared_ptr<apex::Order> _order;
};

int main()
{
  try {
    // create core engine, configured for paper or live trading
    auto services = apex::Services::create(apex::RunMode::paper);

    // ----- Exchange Gateway (Binance) -----

    // create an embedded exchange gateway server
    apex::GxServer gateway{services->realtime_evloop(), services->run_mode()};

    // add Binance exchange to gateway (API key only needed for live trading)
    apex::BinanceSession::Params params;
    // params.api_key_file = apex::user_home_dir()/".apex/binance_key.json";
    gateway.add_venue(params);

    // start the gateway, it will listen for connections from the strategy
    gateway.start();

    // add a route from core engine to the gateway server
    services->gateway_service()->set_default_gateway(gateway.get_listen_port());

    // ----- Strategy -----

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(services.get(), "DEM01");

    // add a bot, which is responsible for trading a single name
    strategy.create_bot<OneOrderDemoBot>(apex::InstrumentQuery(
                                           "BTCUSDT",
                                           apex::ExchangeId::binance));

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // run until user presses control-c
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
