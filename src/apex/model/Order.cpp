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

#include <apex/model/Order.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/Error.hpp>

#include <utility>

namespace apex
{

const char * to_string(Side s) {
  switch (s) {
    case Side::none : return "none";
    case Side::buy : return "buy";
    case Side::sell : return "sell";
    default:
      return "";
  }
}

std::ostream& operator<<(std::ostream& os, Side s) {
  os << to_string(s);
  return os;
}


std::ostream& operator<<(std::ostream& os, OrderCloseReason v)
{
  switch (v) {
    case OrderCloseReason::none:
      return os << "none";
    case OrderCloseReason::cancelled:
      return os << "cancelled";
    case OrderCloseReason::filled:
      return os << "filled";
    case OrderCloseReason::rejected:
      return os << "rejected";
    case OrderCloseReason::lapsed:
      return os << "lapsed";
    case OrderCloseReason::error:
      return os << "error";
    default:
      os << "unknown";
  }
  return os;
}


Order::Order(Services* services, OrderRouter* router, Instrument instrument,
             Side side, double size, double price, TimeInForce tif,
             std::string order_id, void* user_data,
             std::function<void(void*)> user_data_delete_fn)
  : _services(services),
    _router(router),
    _instrument(std::move(instrument)),
    _side(side),
    _size(size),
    _price(price),
    _tif(tif),
    _user_data(user_data),
    _user_data_delete_fn(std::move(user_data_delete_fn)),
    _order_state(OrderState::init),
    _order_id(std::move(order_id))
{
  if (_router == nullptr) {
    THROW("cannot construct Order object with a null OrderRouter");
  }
}


Order::~Order()
{
  if (_user_data)
    _user_data_delete_fn(_user_data);
}


bool Order::cancel()
{
  _cancel_state = OrderCancelState::canceling;
  try {
    _router->cancel_order(*this);
    return true;
  }
  catch (const std::runtime_error& e) {
    LOG_ERROR("error when attempting cancel: " << e.what());
    _cancel_state = OrderCancelState::error;
    return false;
  }
}

const std::string& Order::ticker() const { return _instrument.native_symbol(); }

std::chrono::microseconds Order::duration_since_sent() const
{
  if (_sent_time.empty())
    return {};
  return _services->now() - _sent_time;
}

std::chrono::microseconds Order::duration_live() const
{
  if (_live_time.empty())
    return {};
  return _services->now() - _live_time;
}


void Order::send()
{
  if (_order_state != OrderState::init) {
    THROW("cannot send order, must be in 'init' state");
  }

  _router->send_order(*this);

  _sent_time = _services->now();
  set_state_impl(_services->now(), OrderState::sent);
}


void Order::set_is_closed(Time time, OrderCloseReason reason)
{
  _close_reason = reason;
  set_state_impl(time, OrderState::closed, false, reason);
}

void Order::set_is_rejected(std::string code, std::string text)
{
  Time t;
  _error_code = std::move(code);
  _error_text = std::move(text);
  this->set_is_closed(t, OrderCloseReason::rejected);
}


void Order::set_state_impl(Time time, OrderState new_state, bool with_fill,
                           OrderCloseReason close_reason)
{
  const auto old_state = this->_order_state;

  if (old_state != new_state) {
    if ((new_state == OrderState::live) && (old_state != OrderState::sent)) {
      LOG_WARN(_instrument.native_symbol()
               << ": order " << _order_id
               << ", attempt to set order state LIVE but is not SENT");
      return;
    }

    if (new_state == OrderState::live)
      _live_time = _services->now();

    if (new_state == OrderState::closed)
      _close_reason = close_reason;

    _order_state = new_state;
  }

  int flags = 0;
  if (with_fill)
    flags = flags bitor OrderEvent::Flags::fill;
  if (this->_order_state != old_state)
    flags = flags | OrderEvent::Flags::state_change;

  OrderEvent ev(shared_from_this(), flags, time, old_state, new_state);
  _events.next(ev);
}

const std::string& Order::symbol() const { return _instrument.native_symbol(); }

void Order::apply(const OrderUpdate& update)
{
  if (!update.ext_order_id.empty())
    _exch_order_id = update.ext_order_id;

  Time time = _services->now(); // TODO: take this from `update`
  set_state_impl(time, update.state, false, update.close_reason);
}


void Order::apply(const OrderFill& fill)
{
  _fills.push_back(fill);
  _total_fill_qty += fill.size;

  if (fill.is_fully_filled) {
    set_state_impl(fill.recv_time, OrderState::closed, true,
                   OrderCloseReason::filled);
  } else {
    set_state_impl(fill.recv_time, _order_state,
                   true); // not an actual state change
  }
}


void Order::apply_cancel_reject(std::string code, std::string text)
{
  // TODO: invoke an appropriate Bot callback
  _cancel_state = OrderCancelState::rejected;
  _error_code = std::move(code);
  _error_text = std::move(text);

  LOG_WARN(_instrument.native_symbol()
           << ": order " << _order_id << " RCAN reject-code: " << _error_code
           << ", reject-text: " << _error_text);
}

} // namespace apex
