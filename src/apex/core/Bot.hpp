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

#pragma once

#include <apex/model/Account.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/model/Order.hpp>
#include <apex/model/Position.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/core/Alert.hpp>
#include <apex/util/EventLoop.hpp>

#include <atomic>
#include <memory>
#include <set>
#include <string>

namespace apex
{

class Services;
class Order;
class MarketData;
class Strategy;
class OrderRouter;
class RealtimeEventLoop;


// Track live and closed orders associated with a single Bot.
class OrderCache
{
public:
  [[nodiscard]] bool has_live_orders() const { return !_live_orders.empty(); }

  [[nodiscard]] bool has_pending_orders() const { return !_pending_orders.empty(); }


  void add_new_order(std::shared_ptr<apex::Order> order)
  {
    auto wp = order->weak_from_this();
    _pending_orders.insert(order);
    order->events().subscribe([this, wp](apex::OrderEvent ev) {
      if (ev.is_state_change())
        if (auto sp = wp.lock()) {
          if (sp->is_live()) {
            this->_live_orders.insert(sp);
            this->_pending_orders.erase(sp);
          }
          if (sp->is_closed()) {
            this->_live_orders.erase(sp);
            this->_pending_orders.erase(sp);
          }
        }
    });
  }

  const std::set<std::shared_ptr<apex::Order>>& live_orders()
  {
    return _live_orders;
  }


  const std::set<std::shared_ptr<apex::Order>>& pending_orders()
  {
    return _pending_orders;
  }


private:
  std::set<std::shared_ptr<apex::Order>> _pending_orders;
  std::set<std::shared_ptr<apex::Order>> _live_orders;
};


/* This class is responsible for trading activities on a single name. */
class Bot
{
public:
  Bot(const std::string& bot_typename, Strategy*, Instrument instrument);

  virtual ~Bot();

  // Initialise this Bot, so that it becomes ready for trading.
  virtual void init(double initial_position);

  [[nodiscard]] const std::string& ticker() const
  {
    return _ticker;
  }

  [[nodiscard]] ExchangeId exchange() const
  {
    return _instrument.exchange_id();
  };

  [[nodiscard]] const Instrument& instrument() const { return _instrument; }

  MarketData& market() { return *_mkt; }

  /* Get the last trade price */
  [[nodiscard]] double last_price() const;

  /* Has last trade price */
  [[nodiscard]] bool has_last_price() const;

  [[nodiscard]] std::shared_ptr<Order> create_order(
      Side side, double size, double price, TimeInForce tif,
      void* user_data = nullptr,
      std::function<void(void*)> user_data_delete_fn = {});

  /* Bot event callback handlers */

  virtual void on_tick_trade(MarketData::EventType) {}
  virtual void on_tick_book(MarketData::EventType) {}
  virtual void on_timer() {}
  virtual void on_order_submitted(Order&) {}
  virtual void on_order_live(Order&) {}
  virtual void on_order_closed(Order&) {}
  virtual void on_order_fill(Order&) {}

  Position& position() { return _position; }
  [[nodiscard]] const Position& position() const { return _position; }

  // Round an order price in a passive direction.  For buy side, prices
  // are rounded downward (away from the touch); for sell side, prices are
  // rounded upward (away from the touch).
  [[nodiscard]] double round_price_passive(double raw, Side side) const;

  [[nodiscard]] double round_size(double) const;

  // Return the minimum order size, based on instrument minimum notional. The
  // provided price is the intended order price, and is used to infer the order
  // quantity, based on the minimum order notional associated with the
  // instrument.
  [[nodiscard]] double min_order_size(double price) const;

  [[nodiscard]] bool market_data_ok() const;

  [[nodiscard]] bool om_session_up() const;

  [[nodiscard]] bool has_fx_rate() const;
  [[nodiscard]] double fx_rate() const;
  [[nodiscard]] bool is_fx_ccy() const { return _mkt == _mkt_fx_instr; }

  double net_position_usd() const {
    return (has_last_price() && has_fx_rate())?
      _position.net() * last_price() * fx_rate() : apex::nan;
  }

  bool is_stopping();
  void stop();
  void wait_for_stop();

  EventLoop& event_loop();

protected:
  std::string ccy_value(const char* field, double size, double price);

  Services* _services;
  Strategy * _strategy;
  Instrument _instrument;
  std::string _ticker;
  MarketData* _mkt = nullptr;
  MarketData* _mkt_fx_instr = nullptr;
  OrderRouter* _order_router = nullptr;
  Position _position;
  OrderCache _order_cache;
  AlertBoard _alerts;

  std::atomic<bool> _is_stopping = false;
};

} // namespace apex
