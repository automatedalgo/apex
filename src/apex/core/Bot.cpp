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
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/OrderRouter.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/OrderService.hpp>
#include <apex/core/PersistenceService.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Strategy.hpp>
#include <apex/model/Position.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/RealtimeEventLoop.hpp>

#include <chrono>
#include <cmath>
#include <future>

using namespace std::chrono_literals;

namespace apex
{

Bot::Bot(const std::string& bot_typename, Strategy* strategy,
         Instrument instrument)
  : _services(strategy->services()),
    _strategy(strategy),
    _bot_typename(bot_typename),
    _instrument(std::move(instrument))
{
  bool include_bot_typename = !bot_typename.empty();

  _ticker = _instrument.id();
  if (include_bot_typename)
    _ticker += " (" + bot_typename + ")";
  LOG_INFO(ticker() << ": bot created");
}


Bot::~Bot()
{
  stop(); // attempt to cancel open orders
}


// Initialise this Bot, so that it becomes ready for trading.
void Bot::init(double initial_position)
{
  _position = Position(initial_position);
  LOG_INFO(ticker() << ": initialising bot, startup-position:"
                    << _position.net());

  // setup market data subscription
  _mkt = _services->market_data_service()->find_market_data(_instrument);
  if (!_mkt) {
    THROW("failed to obtain a MarketData instance for instrument "
          << _instrument);
  }

  _mkt->subscribe_events([this](MarketData::EventType event_type) {
    if (!is_stopping()) {
      if (event_type.is_trade()) {
        this->on_tick_trade(event_type);
      }

      if (event_type.is_top()) {
        this->on_tick_book(event_type);
      }
    }
  });

  _order_router = _services->order_router_service()->get_order_router(
    _instrument, _strategy->strategy_id());
  assert(_order_router != nullptr);

  // setup market data subscription for an FX-rate instrument
  if (_services->ref_data_service()->is_fx_rate_instrument(_instrument)) {
    _mkt_fx_instr = _mkt;
  } else {
    auto fx_instruments =
        _services->ref_data_service()->get_fx_rate_instruments(_instrument);
    auto iter = fx_instruments.begin();
    while (iter != std::end(fx_instruments) && !_mkt_fx_instr)
      _mkt_fx_instr = _services->market_data_service()->find_market_data(*iter);

    if (!_mkt_fx_instr)
      LOG_WARN(ticker() << ": failed to find an FX-rate instrument");
  }

  auto timer_interval = 1000ms;
  _services->evloop()->dispatch(timer_interval, [=]() {
    try {
      this->on_timer();
    } catch (std::runtime_error& e) {
      LOG_ERROR("uncaught exception from Bot on_timer, " << e.what());
      // Note, we keep the timer active by continue to return `timer_interval`
      // below.
    }
    return timer_interval;
  });
}

const char* event_code(bool is_fill, OrderState o, OrderCloseReason c)
{
  if (is_fill) {
    if (o == OrderState::closed && c == OrderCloseReason::filled)
      return "XFIL";
    else
      return "FILL";
  } else {
    switch (o) {
      case OrderState::none:
        return "NONE";

      case OrderState::init:
        return "INIT";

      case OrderState::sent:
        return "SENT";

      case OrderState::live:
        return "LIVE";

      case OrderState::closed: {
        switch (c) {
          case OrderCloseReason::none:
            return "XNON";

          case OrderCloseReason::cancelled:
            return "XCAN";

          case OrderCloseReason::filled:
            return "XFIL";

          case OrderCloseReason::rejected:
            return "XREJ";

          case OrderCloseReason::lapsed:
            return "XEXP";

          case OrderCloseReason::error:
            return "XERR";
        }
      }
    }
  }
  return "UNKW";
}

std::string Bot::ccy_value(const char* field, double size, double price)
{
  if (has_fx_rate()) {
    std::ostringstream oss;
    oss << ", " << field << ":"
        << format_double(fx_rate() * size * price, true, 2);
    return oss.str();
  } else {
    return "";
  }
}

std::shared_ptr<Order> Bot::create_order(
    Side side, double size, double price, TimeInForce tif, void* user_data,
    std::function<void(void*)> user_data_delete_fn)

{
  if (!is_finite_non_zero(size))
    throw std::runtime_error("cannot create order, size invalid or zero");
  if (!is_finite_non_zero(price))
    throw std::runtime_error("cannot create order, price invalid or zero");
  if (this->is_stopping())
    throw std::runtime_error("cannot create order when bot is-stopping");


  auto order = _services->order_service()->create(
      _order_router, _instrument, side, size, price, tif,
      _strategy->strategy_id(), user_data, user_data_delete_fn);

  _order_cache.add_new_order(order);

  order->events().subscribe([this](OrderEvent ev) {
    // update internal model
    if (ev.is_fill()) {
      this->_position.apply_fill(ev.order->side(), ev.order->last_fill().size);
      _services->persistence_service()->persist_instrument_positions(
          "XYZ", ev.order->instrument(), _position.net());
    }

    // logging - either it's a fill event or state event
    if (ev.is_fill()) {
      const auto& fill = ev.order->last_fill();
      LOG_INFO(
          ticker() << ": "
                   << "order " << ev.order->order_id() << " "
                   << event_code(true, ev.new_state, ev.order->close_reason())
                   << " "
                   << "side:" << ev.order->side()
                   << ", price:" << format_double(ev.order->price(), true)
                   << ", qty:" << format_double(ev.order->size(), true)
                   << ", qdone:" << format_double(ev.order->filled_size(), true)
                   << ", xprice:" << format_double(fill.price, true)
                   << ", xqty:" << format_double(fill.size, true)
                   << ccy_value("xqtyUsd", fill.size, fill.price)
                   << ", pos:" << _position.net()
                   << ccy_value("posUsd", _position.net(), ev.order->price())
                   << ", exchId:" << ev.order->exch_order_id()
        );
    } else if (ev.is_state_change()) {
      if (ev.new_state == OrderState::closed &&
          ev.order->close_reason() == OrderCloseReason::rejected) {
        LOG_INFO(ticker() << ": "
                 << "order " << ev.order->order_id() << " "
                 << event_code(false, ev.new_state,
                               ev.order->close_reason())
                 << " "
                 << "code: " << ev.order->error_code()
                 << ", text: " << ev.order->error_text());
      } else {
        auto has_exchId = !ev.order->exch_order_id().empty();
        LOG_INFO(ticker()
                 << ": "
                 << "order " << ev.order->order_id() << " "
                 << event_code(false, ev.new_state, ev.order->close_reason())
                 << " "
                 << "side:" << ev.order->side()
                 << ", price:" << format_double(ev.order->price(), true)
                 << ", qty:" << format_double(ev.order->size(), true)
                 << ccy_value("qtyUsd", ev.order->size(), ev.order->price())
                 << ", qdone:" << format_double(ev.order->filled_size(), true)
                 << ", pos:" << _position.net()
                 << ccy_value("posUsd", _position.net(), ev.order->price())
                 << (has_exchId? ", exchId:" : "")
                 << (has_exchId? ev.order->exch_order_id() : "")
                 );
      }
    }

    /* invoke bot callbacks */
    if (ev.is_fill()) {
      this->on_order_fill(*ev.order);
    }

    if (ev.is_state_change()) {
      switch (ev.new_state) {
        case OrderState::none:
        case OrderState::init:
          break;
        case OrderState::sent:
          this->on_order_submitted(*ev.order);
          break;
        case OrderState::live:
          this->on_order_live(*ev.order);
          break;
        case OrderState::closed:
          this->on_order_closed(*ev.order);
          break;
      }
    }
  });

  return order;
}


double Bot::round_price_passive(double raw, Side side) const
{
  if (side == Side::buy) {
    // passive rounding for buy side means rounding to a tick that is
    // lower-than/equal-to the tick; so can use trunc style rounding.
    return _instrument.tick_size.trunc(raw);
  } else {
    // passive rounding on sell side means rounding to a tick that is
    // greater-than/equal-to the tick, so use ceil rounding
  }
  return _instrument.tick_size.ceil(raw);
}


double Bot::round_size(double raw) const
{
  return std::max(0.0, _instrument.lot_size.trunc(raw));
}


double Bot::min_order_size(double price) const
{
  if (price == 0.0)
    return _instrument.minimum_size;
  auto min_notional_size = round_size(_instrument.minimum_notnl / price);
  return std::max(min_notional_size, _instrument.minimum_size);
}


bool Bot::market_data_ok() const
{
  if (_mkt && _mkt->is_good())
    return true;

  return false;
}

bool Bot::om_session_up() const
{
  return _order_router && _order_router->is_up();
}

bool Bot::has_fx_rate() const
{
  return is_fx_ccy() ||
         (!!_mkt_fx_instr && is_finite_non_zero(_mkt_fx_instr->last().price));
}

double Bot::fx_rate() const
{
  return (is_fx_ccy()) ? 1.0 : _mkt_fx_instr->last().price;
}

bool Bot::is_stopping() { return _is_stopping; }

void Bot::stop()
{
  auto sleep_interval = _services->is_backtest()? 0ms: 50ms;

  auto stop_on_eventloop = [this, sleep_interval]() {
    if (!_is_stopping) {
      _is_stopping = true;

      // cancel any open orders
      for (auto& order : _order_cache.live_orders()) {
        if (!order->is_canceling())
          order->cancel();
        std::this_thread::sleep_for(sleep_interval);
      }
      // cancel any pending orders
      for (auto& order : _order_cache.pending_orders()) {
        if (!order->is_canceling())
          order->cancel();
        std::this_thread::sleep_for(sleep_interval);
      }
    }
  };

  if (_services->evloop()->this_thread_is_ev()) {
    stop_on_eventloop();
  } else {
    event_loop().dispatch(stop_on_eventloop);

    // wait for the stop-event to execute, so that the cancel attempts have a
    // chance to execute.
    std::promise<void> stop_has_run;
    event_loop().dispatch([&stop_has_run]() { stop_has_run.set_value(); });
    stop_has_run.get_future().wait();
  }
}

void Bot::wait_for_stop() {
  // This function cannot be called on the event thread, because its job is to
  // wait for event-loop operations to complete.
  assert(not event_loop().this_thread_is_ev());

  // wait for all orders to be closed
  std::promise<void> promise_all_closed;
  apex::EventLoop::timer_fn check = [this, &promise_all_closed] {
    for (auto& order : _order_cache.live_orders())
      if (!order->is_closed())
        return 100ms; // reschedule another check
    for (auto& order : _order_cache.pending_orders())
      if (!order->is_closed())
        return 100ms; // reschedule another check
    promise_all_closed.set_value();
    return 0ms; // don't reschedule
  };
  event_loop().dispatch(100ms, check);
  promise_all_closed.get_future().wait_for(2s);
}

EventLoop& Bot::event_loop() { return *_services->evloop(); }

double Bot::last_price() const {
  return _mkt->last().price;
}

bool Bot::has_last_price() const {
  return _mkt->has_last();
}




} // namespace apex
