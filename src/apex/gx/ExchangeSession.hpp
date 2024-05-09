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
#include <apex/util/Config.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/Services.hpp>
#include <apex/model/Order.hpp>

#include <functional>
#include <memory>

namespace apex
{

class IoLoop;
class SslContext;
class TickTrade;
class TickTop;

class Symbol
{
public:
  std::string native;
};

enum class StreamType { Null = 0, Trades = 't', AggTrades = 'a' };


struct subscription_options {
  StreamType stream_type;
  subscription_options() : stream_type(StreamType::Null) {}
  subscription_options(StreamType stream_type) : stream_type(stream_type) {}
};


struct ExchangeSessionHealth {
  bool is_up = false;
};

class BaseExchangeSession
{
public:
  struct SubmitOrderCallbacks {
    std::function<void(std::string, std::string)> on_rejected;
    std::function<void(OrderUpdate)> on_reply;
  };

  struct EventCallbacks {
    std::function<void(BaseExchangeSession& exchange, std::string order_id,
                       OrderFill)>
        on_order_fill;

    std::function<void(BaseExchangeSession& exchange, std::string order_id,
                       OrderUpdate)>
        on_order_cancel;

  };

  BaseExchangeSession(EventCallbacks callbacks,
                      ExchangeId exchange_id, apex::RunMode run_mode)
    : _callbacks(callbacks),
      _exchange_id(exchange_id),
      _run_mode(run_mode)
  {
    if (_run_mode == RunMode::backtest)
      throw std::runtime_error("exchange session cannot be created in backtest runmode");
  }

  virtual ~BaseExchangeSession() = default;


  virtual void start() = 0;

  virtual void subscribe_account(
      std::function<void(std::vector<AccountUpdate>)> callback) = 0;


  virtual void subscribe_trades(Symbol, subscription_options,
                                std::function<void(TickTrade)>) = 0;

  virtual void subscribe_top(Symbol /*symbol*/, subscription_options,
                             std::function<void(TickTop)> /*callback*/)
  {
    throw std::runtime_error("subscribe_topsubscribe_top not implemented");
  }

  ExchangeId exchange_id() const { return _exchange_id; }

  virtual void submit_order(OrderParams, SubmitOrderCallbacks) = 0;

  virtual void cancel_order(std::string symbol, std::string order_id,
                            std::string ext_order_id, SubmitOrderCallbacks) = 0;

  bool is_paper_trading() const { return _run_mode == RunMode::paper; }

protected:
  EventCallbacks _callbacks;
private:
  ExchangeId _exchange_id;
  RunMode _run_mode;
};

/* Base class for all exchange sessions */
template <typename T>
class ExchangeSession : public std::enable_shared_from_this<T>,
                        public BaseExchangeSession
{

public:
  ExchangeSession(BaseExchangeSession::EventCallbacks callbacks,
                  ExchangeId exchange_id, RunMode run_mode, IoLoop* ioloop,
                  RealtimeEventLoop& event_loop, SslContext* ssl)
     : BaseExchangeSession(std::move(callbacks), exchange_id,
                          run_mode),
       _event_loop(event_loop),
       _ioloop(ioloop),
       _ssl(ssl),
      _curl_requests([](){
         try {
           throw;
         }
         catch (const std::exception& e) {
           LOG_ERROR("caught exception at event loop: ("
                 << demangle(typeid(e).name()) << ") " << e.what());
         }
         catch (...) {
           LOG_ERROR("caught unknown exception at event loop");
         }
         return false; // don't terminate the curl eventloop
       },
         [] { apex::Logger::instance().register_thread_id("curl"); })
  {
  }

  bool is_event_thread() const { return _event_loop.this_thread_is_ev(); }

  void run_on_evloop(std::function<void(T* self)> fn)
  {
    _event_loop.dispatch([fn2 = std::move(fn), weak{this->weak_from_this()}] {
      if (auto sp = weak.lock())
        fn2(sp.get());
    });
  }


protected:
  RealtimeEventLoop& _event_loop;
  IoLoop* _ioloop;
  SslContext* _ssl;
  RealtimeEventLoop _curl_requests;
};

} // namespace apex
