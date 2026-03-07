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

#include <apex/model/ExchangeId.hpp>
#include <apex/model/Order.hpp>
#include <apex/core/RunMode.hpp>
#include <apex/core/OrderRouter.hpp>


#include <cassert>
#include <functional>
#include <memory>


namespace apex {

class MxCancelOrderAck;
class MxCancelOrderRej;
class MxOrderExecution;
class MxOrderExpired;
class MxSubmitOrderAck;
class MxSubmitOrderRej;

class Reactor;
class RealtimeEventLoop;
class SslContext;
class TickTop;
class TickTrade;
class WebsocketClient;
class TimeLog;

bool websock_is_open(const std::shared_ptr<WebsocketClient>& ws);

struct LineHandlerCallbacks {
  std::function<void(const MxSubmitOrderAck&)> on_submit_order_ack;
  std::function<void(const MxSubmitOrderRej&)> on_submit_order_rej;
  std::function<void(const MxCancelOrderAck&)> on_cancel_order_ack;
  std::function<void(const MxCancelOrderRej&)> on_cancel_order_rej;
  std::function<void(const MxOrderExecution&)> on_order_execution;
  std::function<void(const MxOrderExpired&)> on_order_expired;

  void assert_all_defined() const {
    assert(on_submit_order_ack);
    assert(on_submit_order_rej);
    assert(on_cancel_order_ack);
    assert(on_cancel_order_rej);
    assert(on_order_execution);
    assert(on_order_expired);
  }
};

// Base class for all feed handler sessions.
class FeedHandler
{
public:
  FeedHandler(ExchangeId exchange_id,
              apex::RunMode run_mode)
    : _exchange_id(exchange_id),
      _run_mode(run_mode)
  {
    if (_run_mode == RunMode::backtest)
      throw std::runtime_error("FeedHandler cannot be created in backtest runmode");
  }

  virtual void start() = 0;

  virtual void subscribe_trades(std::string) = 0;

  virtual void subscribe_top(std::string) = 0;

  bool is_paper_trading() const { return _run_mode == RunMode::paper; }

protected:
  ExchangeId _exchange_id;
  RunMode _run_mode;
};



template <typename T>
class FeedHandlerImpl : public std::enable_shared_from_this<T>,
                        public FeedHandler
{
public:
  FeedHandlerImpl(ExchangeId exchange_id,
                  Services* services,
                  RunMode run_mode,
                  Reactor* reactor,
                  RealtimeEventLoop* event_loop)
    : FeedHandler(exchange_id, run_mode),
      _services(services),
      _reactor(reactor),
      _event_loop(event_loop)
  {
  }

protected:
  Services* _services;
  Reactor* _reactor;
  RealtimeEventLoop* _event_loop;
};


struct FeedHandlerCallbacks {
  std::function<void(const std::string&, TickTrade&, TimeLog&)> on_trade;
  std::function<void(const std::string&, TickTop &, TimeLog&)> on_top;

  void assert_all_defined() const {
    assert(on_trade);
    assert(on_top);
  }
};


class OrderRouterAdapterImpl : public OrderRouter
{
public:

  OrderRouterAdapterImpl(Services* services)
    :  _services(services) {
  }

  void check_is_up(Order& order);

protected:
  Services* _services;
};


template<typename T>
class OrderRouterAdapter : public OrderRouterAdapterImpl
{
public:
  OrderRouterAdapter(Services* services,
                     std::shared_ptr< T > lh)
    :OrderRouterAdapterImpl(services),
    _lh(std::move(lh))
  {
  }

  void send_order(Order& order) override
  {
    // TODO: review how we are handling immediate rejects - should we return
    // error, throw, or, put a reject on the event loop?

    check_is_up(order);

    OrderParams new_order;
    new_order.symbol = order.symbol();
    new_order.exchange = ExchangeId::binance_usdfut;
    new_order.side = order.side();
    new_order.time_in_force = order.time_in_force();
    new_order.size = order.size();
    new_order.price = order.price();
    new_order.order_id = order.order_id();

    // TODO: add the callbacks here.
    _lh->submit_order(new_order);
  }

  void cancel_order(Order& order) override
  {
    if (!_lh->is_open()) {
      // TODO: handle this here
      return;
    }

    MxCancelOrder cancel;
    cancel.symbol = order.symbol();
    cancel.exchange = ExchangeId::binance_usdfut;
    cancel.order_id = order.order_id();
    cancel.exch_order_id = order.exch_order_id();
    _lh->cancel_order(cancel);
  }

  bool is_up() const override
  {
    return _lh->is_open();
  }

  std::shared_ptr<T> _lh;
};

}
