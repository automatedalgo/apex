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

#include <apex/backtest/TickReplayer.hpp>
#include <apex/core/BacktestService.hpp>
#include <apex/core/BacktestService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Errors.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/util/BacktestEventLoop.hpp>
#include <apex/util/Error.hpp>

namespace apex
{

static bool is_zero(double d) { return fabs(d) < 0.000001; }


SimOrder::SimOrder(Order& order,
                   std::string ext_order_id,
                   double size,
                   double price,
                   Side side)
  : _ext_order_id(ext_order_id),
    _size(size),
    _price(price),
    _side(side),
    _size_remain(size),
    _order(order.weak_from_this())
{

}


bool SimOrder::is_fully_filled() const
{
  return is_zero(_size_remain) || _size_remain < 0.0;
}


SimLadder::SimLadder(Services * services,
                     const Instrument& instrument)
  : _services(services),
    _mkt(nullptr),
    _instrument(instrument)
{
  // setup market data subscription
  _mkt = _services->market_data_service()->find_market_data(instrument);
  if (!_mkt) {
    THROW("SimLadder failed to obtain a MarketData instance for instrument "
          << instrument);
      }

  _mkt->subscribe_events([this](MarketData::EventType event_type) {
    if (true) {
      if (event_type.is_trade()) {
        this->apply_trade(_mkt->last().price,
                          _mkt->last().qty);
      }

      if (event_type.is_top()) {
        // this->on_tick_book(event_type);
      }
    }
  });
}


bool SimLadder::erase_order(std::shared_ptr<SimOrder>& lob_order) {
  HalfOrderBook* hb = (lob_order->side() == Side::buy)? &_bids : &_asks;
  for (auto iter = hb->begin(); iter != hb->end(); ++iter) {
    if (iter->second->ext_order_id() == lob_order->ext_order_id()) {
      hb->erase(iter);
      return true;
    }
  }

  return false;
}


void SimLadder::raise_fill_event(double fill_size,
                                 bool fully_filled, std::shared_ptr<SimOrder> & order)
{
  using namespace std::chrono_literals;
  auto latency = 100ms;

  OrderFill fill;
  fill.is_fully_filled = fully_filled;
  fill.recv_time = {};
  fill.price = order->price();
  fill.size = fill_size;
  _services->evloop()->dispatch(
    latency,
    [order_wp=order->orig_order(), fill](){
      if (auto order_sp = order_wp.lock()) {
        order_sp->apply(fill);
      }
      return 0ms;
    });
}


void SimLadder::apply_trade(double price, double size)
{
  std::list<std::shared_ptr<SimOrder>> fully_filled_orders;
  // scope guard to erase any sim-orders that were fully filled.
  scope_guard on_done([&]() {
    if (!fully_filled_orders.empty()) {
      for (auto & order: fully_filled_orders)
        erase_order(order);
      fully_filled_orders.clear();
    }
  });

  double qty_remain = size;

  // Apply the fill to the bids

  for (auto iter = _bids.rbegin();
       iter != _bids.rend() && qty_remain > 0 && !is_zero(qty_remain);
       ++iter) {
    auto& order = iter->second;

    // note: we assume order has been executed if trades occur at a further away
    // price.

    if (price < order->price()) {
      // apply the fill to qty ahead, ie, the qty already resting on the
      // book ahead of the order
#if 0
      const double qty_ahead_taken = std::min(qty_remain, order->size_ahead());
      order->qty_ahead -= qty_ahead_taken;
      qty_remain -= qty_ahead_taken;
#endif

      // apply the fill to the resting order

      const double qty_fill = std::min(qty_remain, order->size_remain());
      order->apply_fill(qty_fill);
      qty_remain -= qty_fill;

      bool order_fully_filled = is_zero(order->size_remain());

      // raise a fill event
      raise_fill_event(qty_fill, order_fully_filled, order);

      if (order_fully_filled)
        fully_filled_orders.push_back(order);
    } else
      break; /* no prices left to fill */
  }

  // Apply the fill to the offers

  for (auto iter = _asks.begin();
       iter != _asks.end() && qty_remain > 0 && !is_zero(qty_remain);
       ++iter) {
    auto& order = iter->second;

    // note: we assume order has been executed if trades occur at a further away
    // price.

    if (price > order->price()) {
      // apply the fill to qty ahead, ie, the qty already resting on the
      // book ahead of the order
#if 0
      const double qty_ahead_taken = std::min(qty_remain, order->size_ahead());
      order->qty_ahead -= qty_ahead_taken;
      qty_remain -= qty_ahead_taken;
#endif

      // apply the fill to the order
      const double qty_fill = std::min(qty_remain, order->size_remain());
      order->apply_fill(qty_fill);
      qty_remain -= qty_fill;

      bool order_fully_filled = is_zero(order->size_remain());

      // raise a fill event
      raise_fill_event(qty_fill, order_fully_filled, order);

      if (order_fully_filled)
        fully_filled_orders.push_back(order);
    } else
      break; /* no prices left to fill */
  }

}


std::shared_ptr<SimOrder> SimLadder::add_order(Order& order, std::string ext_order_id) {
  using namespace std::chrono_literals;
  auto latency = 100ms;

  auto order_wp = order.weak_from_this();

  // create the resting order object

  auto sim_order = std::make_shared<SimOrder>(
    order,
    ext_order_id,
    order.size(),
    order.price(),
    order.side()
    );


  // auto on_fill_fn = [this, order_wp, latency](double size, double price, bool fully_filled) {

  //   OrderFill fill;
  //   fill.is_fully_filled = fully_filled;
  //   fill.recv_time = {};
  //   fill.price = price;
  //   fill.size = size;
  //   _services->evloop()->dispatch(
  //     latency,
  //     [order_wp, fill](){
  //       if (auto order_sp = order_wp.lock()) {
  //         order_sp->apply(fill);
  //       }
  //       return 0ms;
  //     });
  // };

  // insert price containers

  if (order.side() == Side::buy) {
    _bids.insert({order.price(), sim_order});
  } else if (order.side() == Side::sell) {
    _asks.insert({order.price(), sim_order});
  }

  // ack the order

  _services->evloop()->dispatch(latency,
                                [order_wp,
                                 ext_order_id](){
                                  if (auto order_sp = order_wp.lock()) {
                                    OrderUpdate update;
                                    update.state = OrderState::live;
                                    update.ext_order_id = ext_order_id;
                                    order_sp->apply(update);
                                  }
                                  return 0ms;
                                });

  return sim_order;
}


void SimLadder::remove_order(std::shared_ptr<SimOrder>& order) {
  using namespace std::chrono_literals;
  auto latency = 100ms;

  const std::string ext_order_id = order->ext_order_id();
  bool removed = erase_order(order);

  auto order_wp = order->orig_order();
  if (removed) {
    _services->evloop()->dispatch(
      latency,
      [order_wp, ext_order_id](){
        if (auto order_sp = order_wp.lock()) {
          OrderUpdate update;
          update.state = OrderState::closed;
          update.close_reason = OrderCloseReason::cancelled;
          update.ext_order_id = ext_order_id;
          order_sp->apply(update);
        }
        return 0ms;
      });
  }
  else {
    _services->evloop()->dispatch(
      latency,
      [order_wp](){
        if (auto order_sp = order_wp.lock()) {
          auto text = "order not found";
          auto code = error::e0102;
          order_sp->apply_cancel_reject(code, text);
        }
        return 0ms;
      });
  }
}



SimExchange::SimExchange(Services* services)
  : _services(services)
{

}


void SimExchange::send_order(Order& order) {

  using namespace std::chrono_literals;
  auto latency = 100ms;

  // invent an external order ID
  std::string ext_order_id = "sim_" + order.order_id();

  if (_all_orders.find(ext_order_id) != std::end(_all_orders)) {
    _services->evloop()->dispatch(
      latency,
      [order_wp=order.weak_from_this()](){
        if (auto order_sp = order_wp.lock()) {
          auto reject_code = error::e0102;
          auto reject_text = "duplicate external order ID";
          order_sp->set_is_rejected(reject_code, reject_text);
        }
        return 0ms;
      });
  }

  auto iter = _ladders.find(order.instrument());
  if (iter != std::end(_ladders)) {
    auto resting_order = iter->second->add_order(order, ext_order_id);
    if (resting_order)
      _all_orders.insert({ext_order_id, std::move(resting_order)});
  }
  else {
    THROW("no limit-order-book for " << order.instrument());
  }
}


void SimExchange::cancel_order(Order& order) {
  using namespace std::chrono_literals;
  auto latency = 100ms;

  const std::string & ext_order_id = order.ext_order_id();
  auto iter2 = _all_orders.find(ext_order_id);
  if (iter2 == std::end(_all_orders)) {
    _services->evloop()->dispatch(
      latency,
      [order_wp=order.weak_from_this()]() {
        if (auto order_sp = order_wp.lock()) {
          auto err_code = error::e0103;
          auto err_text = "order not found";
          order_sp->apply_cancel_reject(err_code, err_text);
        }
        return 0ms;
      });

    return;
  }

  auto ladder_iter = _ladders.find(order.instrument());
  if (ladder_iter != std::end(_ladders)) {
    ladder_iter->second->remove_order(iter2->second);
    _all_orders.erase(iter2);
  }
  else {
    THROW("no limit-order-book for " << order.instrument());
  }
}


bool SimExchange::is_up() const {
  return true;
}


OrderRouter* BacktestService::get_order_router(const Instrument& instrument) {

  auto iter = _exchanges.find(instrument.exchange_id());
  if (iter == std::end(_exchanges)) {
    bool inserted = false;
    std::tie(iter, inserted) = _exchanges.insert({instrument.exchange_id(), std::make_unique<SimExchange>(_services)});
    if (inserted) {
      LOG_INFO("created exchange-simulator for " << QUOTE(instrument.exchange_name()));
    }
  }

  iter->second->add_ladder(instrument);

  return iter->second.get();
}


void SimExchange::add_ladder(const Instrument& instrument) {
  auto iter = _ladders.find(instrument);
  if (iter == std::end(_ladders)) {
    auto ladder = std::make_unique<SimLadder>(_services, instrument);
    _ladders.insert({instrument, std::move(ladder)});
  }
}


std::list<apex::Time> get_dates_in_range(apex::Time from, apex::Time upto)
{
  const std::chrono::hours one_day{24};

  auto day_upto = upto.round_to_earliest_day();
  day_upto += one_day;

  auto day_from = from.round_to_earliest_day();

  if (from >= upto)
    throw ConfigError("backtest from-time must be before upto-time");

  std::list<apex::Time> dates;
  auto day = day_from;
  while (day < day_upto) {
    dates.push_back(day);
    day += one_day;
  }

  return dates;
}

BacktestService::BacktestService(Services* services, apex::Time replay_from,
                                 apex::Time replay_upto)
  : _services(services),
    _from(replay_from),
    _upto(replay_upto),
    _dates{get_dates_in_range(_from, _upto)}
{
  LOG_INFO("number of backtest dates: " << _dates.size());
}


BacktestService::~BacktestService()
{
}


void BacktestService::create_tick_replayer(const Instrument& instrument,
                                           MarketData* mktdata,
                                           MdStream stream_type)
{
  std::string base_directory = _services->paths_config().tickdata / "bin1";

  std::pair<Instrument, MdStream> key{instrument, stream_type};

  auto sp = std::make_unique<TickReplayer>(base_directory, instrument, mktdata,
                                           stream_type, _from, _upto, _dates);

  auto file_count = sp->file_count();
  if (!file_count) {
    THROW("no tick-data files found for stream"
          << instrument << "/" << stream_type
          << " for dates "
          << _dates.front().strftime("%Y/%m/%d") << " - "
          << _dates.back().strftime("%Y/%m/%d")
          << ", looking under path '" << base_directory << "'"
      );
  }

  _services->backtest_evloop()->add_event_source(sp.get());

  LOG_INFO("tick-data files found for '"
           << instrument << "/" << stream_type <<"' : " << file_count);
  _replayers.insert({key, std::move(sp)});
}


void BacktestService::subscribe_canned_data(const Instrument& instrument,
                                            MarketData* mktdata,
                                            MdStreamParams stream_params)
{
  if (stream_params.mask == 0) {
    THROW("no market-data streams configured when subscribing to " << instrument);
  }

  if (stream_params.mask & static_cast<int>(MdStream::AggTrades))
    create_tick_replayer(instrument, mktdata, MdStream::AggTrades);

  if (stream_params.mask & static_cast<int>(MdStream::L1))
    create_tick_replayer(instrument, mktdata, MdStream::L1);
}

} // namespace apex
