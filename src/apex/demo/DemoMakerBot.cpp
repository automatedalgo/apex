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

#include <apex/demo/DemoMakerBot.hpp>

#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/RealtimeEventLoop.hpp>

class MyBot : public apex::Bot {
public:
  void on_tick_trade(apex::MarketData::EventType) override {
    /* handle a public trade */
  }

  void on_tick_book(apex::MarketData::EventType) override {
    /* handle an order book update */
  }

  void on_order_closed(apex::Order&) override {
    /* handle own order completed (filled or canceled) */
  }

  void on_order_live(apex::Order&) override {
    /* handle own order live */
  }

  void on_order_fill(apex::Order&) override {
    /* handle own order executed */
  }

  void on_timer() override {
    /* handle regular or one-off timer */
  }
};



namespace apex
{

DemoMakerBot::DemoMakerBot(Strategy* strategy, apex::Instrument instrument)
  : apex::Bot("DemoBot", strategy, instrument)
{
}

void DemoMakerBot::on_order_closed(apex::Order& order)
{
  if (order.is_rejected()) {
    LOG_INFO("handle_order_rejected, code: " << order.error_code() << ", text: "
             << order.error_text());
  } else {
    LOG_DEBUG("on_order_closed: " << order.close_reason());
  }
}


void DemoMakerBot::on_tick_trade(apex::MarketData::EventType)
{
  LOG_INFO("TRADE: " << this->market().last());
}

void DemoMakerBot::manage_pending_orders()
{
  auto max_pending_duration = std::chrono::seconds{5};

  /*
    Orders are considered pending once they have been sent from the algo but
    are yet to receive a response from the GX/exchange (eg, to confirm they
    are live, or closed). In here we detect for such orders, and warn about
    any found.  If an order is pending for too long, it suggests a problem,
    and it should inhibit any further order placement.
  */

  bool set_alert = false;

  for (auto& order : _order_cache.pending_orders()) {
    if (order->duration_since_sent() > max_pending_duration) {
      set_alert = true;
      break;
    }
  }

  // update Alerts
  apex::Alert alert{"order pending too long"};
  if (set_alert)
    _alerts.add(alert);
  else
    _alerts.remove(alert);
}


void DemoMakerBot::manage_live_orders()
{
  // Managing an order is the regular task of determing if the order, at its
  // current price, size and state of execution, should be left on the market
  // or canceled.  It also involves monitoring the progress of orders, as they
  // progress from state SENT to LIVE.

  // For this example strategy, we demonstrate basic order management.  For
  // any live order, we check whether the latest market bid price has moved
  // too far from the resting-order's bid price, implying that our resting
  // order will never get hit, and so we cancel.  This price at which to
  // cancel was determined when the order was first created, and was stored in
  // the OrderExtraInfo structure associated with the order.
  for (auto& order : _order_cache.live_orders()) {
    auto bid = market().bid();
    auto auto_cancel_price =
      ((OrderExtraInfo*)order->user_data())->cancel_price;

    // TODO: what happens if the cancel fails?  Do we make another attempt?
    if ((bid > auto_cancel_price) && !order->is_canceling()) {
      LOG_INFO(ticker() << ": order " << order->order_id()
               << " canceling (too far away)");
      if (!order->cancel()) {
        LOG_ERROR("order cancel failed (internal error)");
      }
    }
  }
}


void DemoMakerBot::manage_order_initiation()
{
  // if the strategy is stopping, don't send any new orders
  if (is_stopping()) {
    return;
  }

  if (not _alerts.empty()) {
    LOG_WARN("disabled order initiation due to Bot alerts; current alerts:");
    _alerts.log();
    return;
  }

  // If market data bad, or we don't have an FX rate, cannot place orders
  if (!market_data_ok() || !has_fx_rate()) {
    return;
  }

  const auto cur_pos_usd = position().net() * market().last().price * fx_rate();
  const auto distance_usd = _target_position_usd - cur_pos_usd;

  // limit single order size to some fraction of our target position
  const auto max_order_usd = _target_position_usd * 0.1;

  // target price is just below the bid price.
  double order_price_distance_pct = 1.0;

  // the desired order value, in USD; if this is negative number, then we have
  // reached our target and no longer need to place orders
  double order_size_usd = std::min(distance_usd, max_order_usd);
  if (order_size_usd < 0.0) {
    return;
  }

  // choose the order price
  const auto ref_price = market().bid();

  const double raw_price =
    ref_price * (1.0 - (order_price_distance_pct / 100.0));
  const double rounded_price = round_price_passive(raw_price, apex::Side::buy);

  // construct the price at which we will automatically cancel the order
  // const auto cancel_price = rounded_price +
  // (instrument().tick_size.as_double() * 300);
  const auto cancel_price = market().last().price * 1.1;

  // size the order, based on price and desired order value
  const double raw_order_size_qty =
    order_size_usd / (rounded_price * fx_rate());
  const double rounded_size = round_size(raw_order_size_qty);


  // don't try to create and an order with invalid size / price
  if (rounded_price <= 0.0 || rounded_size <= 0.0 ||
      rounded_size < min_order_size(rounded_price)) {
    return;
  }

  // create user_data for our order (and a deletion handler)
  std::unique_ptr<OrderExtraInfo> order_extra{new OrderExtraInfo()};
  order_extra->cancel_price = cancel_price;
  auto order_extra_releaser = [](void* p) {
    auto* order_extra = (OrderExtraInfo*)p;
    delete (OrderExtraInfo*)order_extra;
  };

  // create the local order object; note, this does not cause the object to be
  // sent to the exchange
  auto order = create_order(apex::Side::buy, rounded_size, rounded_price,
                            apex::TimeInForce::gtc, order_extra.get(),
                            order_extra_releaser);

  // now that the order has been created, management of the OrderExtraInfo
  // resource has been transferred, so release management from local memory
  // cleanup (the shared pointer)
  order_extra.release();

  // Send the order to the exchange.  This is an asynchronous operation.  The
  // order object will now be in the pending state.
  order->send();

  // as a safety feature, we set a maximum lifetime on this order, which we do
  // by setting a timer which when expires, will cancel the order
  _services->evloop()->dispatch(
    std::chrono::seconds(30), [wp = order->weak_from_this()]() {
      if (auto sp = wp.lock()) {
        if (sp->is_live() && !sp->is_canceling()) {
          if (!sp->cancel()) {
            LOG_WARN("order cancel had internal reject");
          }
        }
      }
      return std::chrono::seconds(0);
    });
}


void DemoMakerBot::on_timer()
{
  if (!om_session_up()) {
    return;
  }

  if (_order_cache.has_pending_orders()) {
    manage_pending_orders();
  }

  if (_order_cache.has_live_orders()) {
    manage_live_orders();
  }

  // if we have no live/pending then attempt order creation
  if (!_order_cache.has_pending_orders() && !_order_cache.has_live_orders()) {
    manage_order_initiation();
  }
}



}
