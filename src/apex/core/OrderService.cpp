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

#include <apex/core/Core.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/EventLoop.hpp>

#include <iomanip>
#include <iostream>
#include <utility>

namespace apex
{

class FullUniqueOrderIdGenerator
{
public:
  explicit FullUniqueOrderIdGenerator(apex::Core* core) :
    _core(core),
    _order_counter(0) {
    // TODO: need session ID
    // TODO: need start-up-time
    //
  }

  template <typename T>
  static std::string int_to_hex(T i) {
    std::stringstream ss;
    ss << std::setfill('0')
       << std::setw(sizeof(int)*2)
       << std::hex << i;
    return ss.str();

  }
  std::string next(const std::string & strategy_id) {
    Time startup_time = _core->startup_time();

    int epoch_sec = int(startup_time.as_epoch_ms().count() / 1000);

    std::string hex = int_to_hex(epoch_sec);

    std::ostringstream oss;
    if (_order_counter == 0xFFFFFFFF) {
      THROW("no more order IDs available, cannot create order");
    }
    oss << strategy_id << hex << std::setfill('0') << std::setw(8) << std::hex << _order_counter++;
    return oss.str();
  }

private:
  apex::Core* _core;
  uint32_t _order_counter;
};

class ClientOrderIdGenerator
{
  int _intra_sec_order_count = 0;
  uint32_t _last_epoch_sec = 0;

public:
  explicit ClientOrderIdGenerator(apex::Core* core) : _core(core) {}


  std::string next(const std::string& strategy_id)
  {
    auto now = _core->now();

    const uint32_t current_sec = uint32_t(now.as_epoch_ms().count() / 1000);

    _intra_sec_order_count =
      (current_sec == _last_epoch_sec) ? _intra_sec_order_count + 1 : 0;
    _last_epoch_sec = current_sec;

    auto sec_part = int32_to_base16(current_sec);
    auto num_part = int32_to_base16(_intra_sec_order_count & 0xfff);


    std::ostringstream oss;
    oss << strategy_id << sec_part << num_part.substr(4);
    std::string full = oss.str();

    return full;
  }

private:
  apex::Core* _core;
};


OrderService::OrderService(Core* core)
  : _core(core),
    _order_id_src(std::make_unique<FullUniqueOrderIdGenerator>(_core))
{
  auto interval = std::chrono::minutes(60);
  _core->evloop()->dispatch(interval, [this, interval]() {
    this->background_tasks();
    return interval;
  });
}


OrderService::~OrderService() = default;


std::shared_ptr<Order> OrderService::create(
  OrderRouter* router, Instrument instrument, Side side, double size,
  double price, TimeInForce tif, const std::string& strategy_id, void* user_data,
  std::function<void(void*)> user_data_delete_fn)
{
  std::ostringstream oss;
  oss << _order_id_src->next(strategy_id);

  auto order =
    std::make_shared<Order>(_core, router, instrument, side, size, price,
                            tif, oss.str(), user_data, user_data_delete_fn);

  auto wp = order->weak_from_this();
  order->events().subscribe([this, wp](OrderEvent ev) {
    if (ev.is_state_change()) {
      auto sp = wp.lock();
      if (sp && sp->is_closed()) {
        auto iter = _orders.find(sp->order_id());
        if (iter != std::end(_orders)) {
          _orders.erase(iter);
          _dead_order_ids.insert({sp->order_id(), _core->now()});
        }
      }
    }
  });

  _orders.insert({order->order_id(), order});

  return order;
}


std::shared_ptr<Order> OrderService::find_order(const std::string& order_id)
{
  auto iter = _orders.find(order_id);
  return (iter != std::end(_orders)) ? iter->second : nullptr;
}


void OrderService::route_fill_to_order(const std::string& order_id,
                                       OrderFill& fill)
{
  auto order = find_order(order_id);

  // we base the callback on the shared-pointer, rather than an iterator, into
  // the container, just in case the iterator gets deleted during the callbacks
  // that get invoked when apply() is called - which can happen if the order is
  // now closed.
  if (order) {
    order->apply(fill);
  } else {
    LOG_WARN("dropping order-fill, because no order found with orderId "
             << QUOTE(order_id));
  }
}


void OrderService::route_update_to_order(const std::string& order_id,
                                         OrderUpdate& update)
{
  auto order = find_order(order_id);

  // we base the callback on the shared-pointer, rather than an iterator, into
  // the container, just in case the iterator gets deleted during the callbacks
  // that get invoked when apply() is called - which can happen if the order is
  // now closed.
  if (order) {
    order->apply(update);
  } else {
    auto iter = _dead_order_ids.find(order_id);

    if (iter != std::end(_dead_order_ids)) {
      // appears we have received an order-update for an order that has already
      // been closed; eg, this is typically because its a websocket cancel, but
      // the on-rest cancel has already been received; its safe to ignore these.
    } else {
      LOG_WARN("dropping order-update, because no order found with orderId "
               << QUOTE(order_id));
    }
  }
}


void OrderService::process_submit_order_ack(ExchangeId exch_id,
                                            const MxSubmitOrderAck& ack)
{
  auto order = find_order(ack.order_id);
  if (order) {
    std::pair<ExchangeId, std::string> key {exch_id,ack.exch_order_id};
    // auto key = std::make_pair<>(exch_id, ack.exch_order_id);
    // TODO: check does not exist

    _orders_by_exch_order_id.insert({key, order});
    order->apply(ack);
  }
  else {
    // An order ack matching this already exists
    if (!(ack.flags & MxSubmitOrderAck::possible_duplicated))
      LOG_WARN("ingoring duplicate order ack, order_id: " << ack.order_id);
  }
}


void OrderService::process_order_expired(ExchangeId exch_id,
                                         const MxOrderExpired& exp)
{
  std::pair<ExchangeId, std::string> key {exch_id, exp.exch_order_id};

  if (auto iter = _orders_by_exch_order_id.find(key);
      iter != _orders_by_exch_order_id.end()) {
    OrderUpdate update;
    update.state = OrderState::closed;
    update.close_reason = OrderCloseReason::lapsed;
    iter->second->apply(update);

    if (iter->second->is_closed()) {
      _orders_by_exch_order_id_closed.insert({key, std::move(iter->second)});
      _orders_by_exch_order_id.erase(iter);
    }
  }
  else {
    if (auto iter = _orders_by_exch_order_id_closed.find(key);
        iter != _orders_by_exch_order_id_closed.end()) {

      // we have received an order-cancel update for an order that has already
      // been cancelled, most likely because we earlier processes an
      // order-cancel-ack
      _orders_by_exch_order_id_closed.erase(iter);
    }
    else {
      LOG_WARN("ignoring order expired, order not found, exch_order_id: " << exp.exch_order_id);
    }
  }
}


void OrderService::process_order_execution(ExchangeId exch_id,
                                           const MxOrderExecution& exec)
{
  std::pair<ExchangeId, std::string> key {exch_id, exec.exch_order_id};

  auto iter = _orders_by_exch_order_id.find(key);

  if (iter != _orders_by_exch_order_id.end()) {
    iter->second->apply_order_execution(exec.qty,
                                        exec.price,
                                        exec.fullfill==MxOrderExecution::filled);
  }
  else {
    LOG_ERROR("dropped execution for unknown order, "
              << " exch_order_id: " << exec.exch_order_id
              << ", exchange: " << exch_id);
  }
}


void OrderService::process_submit_order_rej(const MxSubmitOrderRej& msg)
{
  auto order = find_order(msg.order_id);
  if (order) {
    order->apply_order_reject(msg.exch_error_code, msg.exch_error_text);
  }
}


void OrderService::process_cancel_order_ack(const MxCancelOrderAck& ack)
{
  auto order = find_order(ack.order_id);
  if (order) {
    OrderUpdate update;
    update.state = OrderState::closed;
    update.close_reason = OrderCloseReason::cancelled;
    order->apply(update);

    if (order->is_closed()) {
      std::pair<ExchangeId, std::string> key {
        order->instrument().exchange_id(),
        order->exch_order_id()};
      auto iter = _orders_by_exch_order_id.find(key);
      if (iter != _orders_by_exch_order_id.end()) {
        _orders_by_exch_order_id_closed.insert({key, std::move(iter->second)});
        _orders_by_exch_order_id.erase(iter);
      }
    }
  }
}

void OrderService::process_cancel_order_rej(const MxCancelOrderRej& rej)
{
  auto order = find_order(rej.order_id);
  if (order) {
    order->apply_cancel_reject(rej.exch_error_code,
                               rej.exch_error_text);
  }
  else {
    LOG_WARN("ignoring cancel order reject for unknown order ID " <<
             rej.order_id);
  }
}


void OrderService::background_tasks()
{
  /* event thread */

  LOG_INFO("number of active orders: " << std::size(_orders));
  LOG_INFO("number of exch_order_id entries: " << std::size(_orders_by_exch_order_id));
  LOG_INFO("number of exch_order_id entries (closed): " << std::size(_orders_by_exch_order_id_closed));
}


} // namespace apex
