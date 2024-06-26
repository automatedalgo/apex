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
class SingleOrderBot : public apex::Bot
{
public:
  std::shared_ptr<apex::Order> _order;

  SingleOrderBot(apex::Strategy* strategy, const apex::Instrument& instrument)
    : apex::Bot("SingleOrderBot", strategy, instrument) {}

  void on_timer() override
  {
    // Every 1 second run the Bot logic. This basic example checks either (1)
    // are we ready to send an order, or (2) can we cancel an existing order. If
    // our position is greater than $100, orders are not sent.
    if (!_order) {
      if (abs(net_position_usd()) < 100)
        create_and_send_order();
    }
    else
      manage_existing_order();
  }

  void create_and_send_order()
  {
    if (!market_data_ok() || !has_fx_rate()) {
      LOG_WARN(ticker() << ": waiting for market & fx data");
      return;
    }

    if (is_stopping())
      return;

    // desired value of the order USD
    auto order_usd = 25.0;

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

  void manage_existing_order()
  {
    // if order has been on the market too long, cancel
    if ((_order->duration_live() > std::chrono::seconds(10)) &&
        !_order->is_closed_or_canceling())
      _order->cancel();

    // if order is closed, delete; after this, we can create next order
    if (_order->is_closed())
      _order.reset();
  }
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
    params.api_key_file = apex::user_home_dir()/".secrets/binance_key.json";
    gateway.add_venue(params);

    // start the gateway, it will listen for connections from the strategy
    gateway.start();

    // add a route from core engine to the gateway server
    services->gateway_service()->set_default_gateway(gateway.get_listen_port());

    // ----- Strategy -----

    // create a Strategy object, which is a container for individual Bots
    apex::Strategy strategy(services.get(), "QUOT1");

    // add a bot, which is responsible for trading a single name
    strategy.create_bot<SingleOrderBot>(apex::InstrumentQuery(
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
