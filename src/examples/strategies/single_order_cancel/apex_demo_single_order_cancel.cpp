/* Copyright 2026 Automated Algo (www.automatedalgo.com)

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

/* This is a basic demo Bot that places a passive buy order. The order is
   positioned far from the bid/ask to prevent execution.  After 20 seconds the
   order is cancelled. This example shows how orders are priced, sized, created,
   sent and then later managed.
*/
class SingleOrderCancelDemoBot : public apex::Bot
{
public:

  SingleOrderCancelDemoBot(apex::Strategy* strategy,
                     const apex::Instrument& instrument)
    : apex::Bot("SingleOrderCancel", strategy, instrument) {}


  void on_order_live(apex::Order& order) override {
    LOG_INFO(name() << ": order live: " << order.order_id());
  }


  void on_order_closed(apex::Order& order) override {
    LOG_INFO(name() << ": order closed: " << order.order_id()
             << ", reason: " << order.close_reason());
  }


  /* Bot's on-timer callback, called periodically by the core engine. We always
   * use this to manage live orders, and also in this current Bot to initiate
   * and order send. Periodically the Bot's on_timer method is called.  This is
   * where we implement the logic of the Bot.  Which is to check if we have sent
   * or order yet.  And if an order is sent, we then manage its life-time. If
   * the order gets rejected we don't attempt to send another. */
  void on_timer() override
  {
    using namespace std::chrono_literals;

    if (!_order) {
      // We have not created and order object, so create and send
      _create_and_send_order();
      return;
    }

    if (!_order->is_closed_or_canceling())
    {
      // The order is still 'live', so code here is order life-cycle
      // management. Our only management logic is to cancel the order if it's
      // been alive for too long.
      if (_order->duration_live() > 10s)
      {
        if (_order->cancel_reject_count() >= 3) {
          LOG_ERROR("unable to cancel order after " << _order->cancel_reject_count() << " attempts");
          _order->set_is_closed(_core->now(), apex::OrderCloseReason::error);
        }
        else  {
          if (!_order->cancel()) {
            LOG_ERROR("failed to send order-cancel request");
          }
        }
      }
      return;
    }
  }

private:

  void _create_and_send_order()
  {
    // We must never send and order if the current L1 market data is bad
    // (crossed book, missing bid or ask).  And if we don't have an FX rate,
    // then we can't size our order.
    if (!market_data_ok() || !has_fx_rate()) {
      LOG_INFO(name() << ": waiting for market data");
      return;
    }

    // Our Bot might be in the process of stopping (due to user control), if so,
    // we don't want to send any new order.
    if (is_stopping())
      return;

    // desired value of the order in USD
    auto order_usd = 25.0;

    // choose price 1% away from last trade, so that it doesn't execute
    double distance_pct = 1;

    // calculate our passive limit price
    double price = round_price_passive(last_price() * (1.0-distance_pct/100.0),
                                       apex::Side::buy);

    // don't send order if calculated order is zero
    if (apex::dbl_is_zero(price)) {
      LOG_WARN(name() << ": cannot send, calculated order price is " << price);
      return;
    }

    // size the order quantity, based on target price and value
    double qty = round_size(order_usd / (price * fx_rate()));

    // don't send order if calculated order qty is zero
    if (apex::dbl_is_zero(qty)) {
      LOG_WARN(name() << ": cannot send, calculated order qty is " << qty);
      return;
    }

    // construct an order object (this does not cause it to be sent)
    _order = create_order(apex::Side::buy, qty, price, apex::TimeInForce::gtc);

    // send order to the exchange (this is an asynchronous operation - we will
    // know when it becomes live via a later callback)
    auto status = _order->send();

    if (!status) {
      LOG_WARN(name() << ": failed to send order, " << status.err_msg());
    }
  }

  // Our order instance.
  std::shared_ptr<apex::Order> _order;
};


int main()
{
  try {
    // create core engine, configured for paper or live trading
    apex::Logger::configure(apex::Logger::info);
    auto core = apex::Core::create(apex::RunMode::paper);

    // ----------------------------------------------------------------------
    // CONFIGURE CORE SERVICES
    // ----------------------------------------------------------------------

    // Add route to Binance exchange
    apex::OrderRouterConfig binance_config;
    binance_config.api_key_file = apex::user_home_dir() / ".secrets/binance_spot.json";
    core->order_router_service()->add_binance_spot(binance_config);

    // Add market data feed from Binance Spot
    apex::FeedConfig feed_config;
    feed_config.type = "Binance";
    core->market_data_service()->add_feed(feed_config, {"binance"});

    // ----------------------------------------------------------------------
    // STRATEGY
    // ----------------------------------------------------------------------

    // create a Strategy object, which is a container for individual bots
    apex::Strategy strategy(core.get(), "DEM01");

    // add a single bot - first query an instrument, then create a bot for it
    auto instrument = apex::InstrumentQuery("ETHBTC", apex::ExchangeId::binance);
    strategy.create_bot<SingleOrderCancelDemoBot>(instrument);

    // initialise all bots, so they can begin trading
    strategy.init_bots();

    // ----------------------------------------------------------------------
    // RUN
    // ----------------------------------------------------------------------

    // Start the main engine and run until killed by the user
    core->run();
  }
  catch (std::exception& e) {
    std::cout << "exception: " << e.what() << std::endl;
    return 1;
  }
  catch (...) {
    std::cout << "unknown exception: " << std::endl;
    return 1;
  }
}
