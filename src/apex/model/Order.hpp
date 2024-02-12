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

#include <apex/model/Instrument.hpp>
#include <apex/util/Time.hpp>
#include <apex/util/rx.hpp>

#include <memory>

namespace apex
{
class Order;
class Services;
class OrderRouter;

enum class Side { none = 0, buy, sell };

std::ostream& operator<<(std::ostream&, Side s);

const char * to_string(Side);

std::ostream& operator<<(std::ostream&, Side s);

enum class TimeInForce : unsigned int { none = 0, ioc, fok, gtc };

enum class OrderType { limit, market };

enum class OrderCloseReason : unsigned int {
  none = 0,
  cancelled,
  filled,
  rejected,
  lapsed, // eg, expired or unsolicited cancel
  error
};

std::ostream& operator<<(std::ostream&, OrderCloseReason);

enum class OrderCancelState : unsigned int {
  none = 0,
  canceling, // request is pending
  rejected,  // request rejected by exchange
  canceled,  // request successful, order is canceled
  error      // request failed internally
};


enum class OrderState : unsigned int { none = 0, init, sent, live, closed };


struct OrderEvent {

  enum Flags { state_change = 1 << 0, fill = 1 << 1 };

  [[nodiscard]] bool is_fill() const { return flags & Flags::fill; }

  [[nodiscard]] bool is_state_change() const
  {
    return flags & Flags::state_change;
  }

  // retain a shared-ptr to the Order, so that the Order instance cannot
  // be deleted before or during the call of an OrderEvent callback.
  std::shared_ptr<Order> order;
  int flags;
  Time time;
  OrderState old_state;
  OrderState new_state;

  OrderEvent()
    : order(nullptr),
      flags{0},
      time{},
      old_state(OrderState::init),
      new_state(OrderState::init)
  {
  }

  OrderEvent(std::shared_ptr<Order> order, int flags, Time time,
             OrderState old_state, OrderState new_state)
    : order(std::move(order)),
      flags(flags),
      time(time),
      old_state(old_state),
      new_state(new_state)
  {
  }
};
// inline std::ostream& operator<<(std::ostream& os, OrderEvent::Type type) {
//   switch (type) {
//     case OrderEvent::Type::state_change : os << "state"; break;
//     case OrderEvent::Type::fill : os << "fill"; break;
//   }
//   return os;
// }


struct OrderParams {
  std::string symbol;
  ExchangeId exchange;
  Side side = Side::none;
  OrderType order_type = OrderType::limit;
  TimeInForce time_in_force = TimeInForce::fok;
  double size = 0.0;
  double price = 0.0;
  std::string order_id;
};


struct OrderUpdate {
  // TODO: this needs to have a recv_time
  OrderState state = OrderState::none;
  OrderCloseReason close_reason = OrderCloseReason::none;
  std::string ext_order_id;
};

struct OrderFill {
  bool is_fully_filled = false;
  Time recv_time = {};
  double price = 0.0;
  double size = 0.0;
};

/* Responsible for representing an Order, including the attributes of that
 * order, state of that order, actions and events.
 *
 * An Order instance will always be associated with an exchange. */
class Order : public std::enable_shared_from_this<Order>
{
public:
  Order(Services* services, OrderRouter* session, Instrument instrument,
        Side side, double size, double price, TimeInForce tif,
        std::string order_id, void* user_data = nullptr,
        std::function<void(void*)> user_data_delete_fn = {});

  ~Order();

  void* user_data() { return _user_data; }

  const std::string& symbol() const;


  [[nodiscard]] const Instrument& instrument() const { return _instrument; }
  [[nodiscard]] Side side() const { return _side; }
  [[nodiscard]] double size() const { return _size; }
  [[nodiscard]] double price() const { return _price; }
  [[nodiscard]] TimeInForce time_in_force() const { return _tif; }
  [[nodiscard]] OrderCloseReason close_reason() const { return _close_reason; }
  [[nodiscard]] std::string error_code() const { return _error_code; }
  [[nodiscard]] std::string error_text() const { return _error_text; }

  [[nodiscard]] const std::string& ticker() const;

  /* Send this order to the exchange */
  void send();

  /* Attempt to cancel this order. Returns true if the request was internally
   * sent, else false if there was an internal error. Cancel is an asynchronous
   * operation (because it involves the exchange), so success/rejection of the
   * request will only be determined later. */
  bool cancel();

  // TODO: change this to have an appy_***** name, like the other apply_
  // methods.
  void set_is_rejected(std::string code, std::string text);

  void set_is_closed(Time time, OrderCloseReason reason);

  bool is_closed() const { return _order_state == OrderState::closed; }

  bool is_live() const { return _order_state == OrderState::live; }

  bool is_closed_or_canceling() const {
    return is_closed() || is_canceling();
  }

  bool is_rejected() const
  {
    return _order_state == OrderState::closed &&
           _close_reason == OrderCloseReason::rejected;
  }

  bool is_canceling() const
  {
    return _cancel_state == OrderCancelState::canceling;
  }

  bool is_cancel_rejected() const
  {
    return _cancel_state == OrderCancelState::rejected;
  }

  rx::observable<OrderEvent>& events() { return _events; }

  // Engine's internal order ID
  const std::string& order_id() const { return _order_id; }

  // The exchange's order ID
  const std::string& exch_order_id() const { return _exch_order_id; }
  const std::string& ext_order_id() const { return _exch_order_id; }

  OrderState state() const { return _order_state; }


  std::chrono::microseconds duration_since_sent() const;
  std::chrono::microseconds duration_live() const;

  // Elapse time since initial cancel request sent
  std::chrono::milliseconds duration_canceling() const
  {
    // TODO: implement me
    return {};
  }


  void apply(const OrderUpdate&);
  void apply_cancel_reject(std::string code, std::string text);
  void apply(const OrderFill&);


  double filled_size() const { return _total_fill_qty; }
  double remain_size() const { return _size - filled_size(); }
  bool has_fills() const { return !_fills.empty(); }

  OrderFill last_fill() const
  {
    if (!_fills.empty())
      return _fills.back();
    else
      return {};
  }

private:
  void set_state_impl(Time time, OrderState new_state, bool with_fill = false,
                      OrderCloseReason reason = OrderCloseReason::none);

  Services* _services;
  OrderRouter* _router;
  Instrument _instrument;
  Side _side;
  double _size;
  double _price;
  TimeInForce _tif;
  void* _user_data;
  std::function<void(void*)> _user_data_delete_fn;
  OrderState _order_state;
  OrderCancelState _cancel_state = OrderCancelState::none;
  OrderCloseReason _close_reason = OrderCloseReason::none;
  std::string _order_id;
  std::string _exch_order_id;
  OrderEvent _last_event;
  rx::subject<OrderEvent> _events;
  std::string _error_code;
  std::string _error_text;
  Time _sent_time;
  Time _live_time; // time order went live
  double _total_fill_qty = 0.0;
  std::list<OrderFill> _fills;
};

} // namespace apex
