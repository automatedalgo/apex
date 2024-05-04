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

#include <apex/core/MockMatchingEngine.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>

#include <cassert>
#include <cmath>

namespace apex
{


static bool is_zero(double d) { return fabs(d) < 0.000001; }

class MockLimitOrder : public std::enable_shared_from_this<MockLimitOrder>
{
public:
  MockLimitOrder(std::string symbol, std::string client_order_id, double size,
                 double price, Side side,
                 MockMatchingEngine::OnFillFn on_fill_callback,
                 MockMatchingEngine::OnUnsolCancelFn on_unsol_cancel_fn)
    : _symbol(std::move(symbol)),
      _client_order_id(std::move(client_order_id)),
      _size(size),
      _price(price),
      _side(side),
      _size_ahead(0), // TODO: enable if we have order-depth feed
      _size_remain(size),
      _on_fill_fn(on_fill_callback),
      _on_unsol_cancel_fn(on_unsol_cancel_fn)
  {
  }

  const std::string& client_order_id() const { return _client_order_id; }
  const std::string& symbol() const { return _symbol; }
  Side side() const { return _side; }

  double size() const { return _size; }
  double price() const { return _price; }

  double size_ahead() const { return _size_ahead; }

  double size_remain() const { return _size_remain; }

  void apply_fill(double d)
  {
    _size_remain -= d;

    LOG_INFO("mock: order fill "
             << _symbol << " id:" << _client_order_id << ", qexec: " << d
             << ", qremain: " << format_double(_size_remain, true, 12)
             << ", more precision: " << 1000000.0 * _size_remain << ", this:"
             << this << ", is_filly_filled: " << is_fully_filled());
    if (_on_fill_fn)
      _on_fill_fn(d, is_fully_filled());
  }

  bool is_fully_filled() const
  {
    return is_zero(_size_remain) || _size_remain < 0.0;
  }

  void apply_unsol_cancel()
  {
    if (_on_unsol_cancel_fn)
      _on_unsol_cancel_fn();
  }

  bool has_unsol_cancel_fn() const { return (bool)_on_unsol_cancel_fn; }

private:
  std::string _symbol;
  std::string _client_order_id;
  double _size;
  double _price;
  Side _side;
  double _size_ahead; // order book qty ahead of us, if available
  double _size_remain;
  MockMatchingEngine::OnFillFn _on_fill_fn;
  MockMatchingEngine::OnUnsolCancelFn _on_unsol_cancel_fn;
};


MockMatchingEngine::MockMatchingEngine(RealtimeEventLoop& ev) : _event_loop(ev) {}


std::string MockMatchingEngine::add_order(const std::string& symbol,
                                          std::string client_order_id,
                                          double size, double price, Side side,
                                          OnFillFn on_fill_fn,
                                          OnUnsolCancelFn on_unsol_cancel_fn)
{
  if (auto iter = _all_orders.find(client_order_id);
      iter != std::end(_all_orders)) {
    LOG_WARN("rejecting mock order; order already live for ID  '"
             << client_order_id << "'");
    return "duplicate client-order-id";
  }

  auto enable_order_ack = true;

  if (!enable_order_ack) {
    LOG_INFO("mock: not acking order " << client_order_id);
    return {};
  }

  auto order = std::make_shared<MockLimitOrder>(
      symbol, std::move(client_order_id), size, price, side,
      std::move(on_fill_fn), std::move(on_unsol_cancel_fn));


  if (order->has_unsol_cancel_fn()) {
    _event_loop.dispatch(
        std::chrono::seconds(60), [wp = order->weak_from_this(), this]() {
          if (auto sp = wp.lock()) {
            if (this->remove_order(sp->client_order_id())) {
              LOG_INFO("mock: order unsol canceled: " << sp->client_order_id());
              sp->apply_unsol_cancel();
            }
          }
          return std::chrono::seconds(0);
        });
  }

  // if (auto iter = _all_orders.find(client_order_id);
  //     iter != std::end(_all_orders)) {
  // }

  // insert into order ID and price containers
  _all_orders.insert({order->client_order_id(), order});

  auto& book = _books[order->symbol()];

  if (!book.market_data_ticking) {
    LOG_WARN("mock-matching-engine not ticking for " << QUOTE(symbol));
  }

  if (order->side() == Side::buy) {
    book.bids.insert({order->price(), order});
  } else if (order->side() == Side::sell) {
    book.asks.insert({order->price(), order});
  }

  return {};
}


void MockMatchingEngine::remove_completed_orders()
{
  // remove all orders that are fully filled
  for (auto order_id : _completed_orders) {
    auto order_iter = _all_orders.find(order_id);
    if (order_iter != std::end(_all_orders) &&
        order_iter->second->is_fully_filled()) {
      auto& order = order_iter->second;

      HalfOrderBook* hb = (order->side() == Side::buy)
                              ? &_books[order->symbol()].bids
                              : &_books[order->symbol()].asks;

      auto range = hb->equal_range(order->price());
      for (auto i = range.first; i != range.second;) {
        if (i->second->is_fully_filled()) {
          hb->erase(i++);
        } else
          i++;
      }
      _all_orders.erase(order_iter);
    }
  }

  _completed_orders.clear();
}


bool MockMatchingEngine::remove_order(const std::string& client_order_id)
{
  auto order_iter = _all_orders.find(client_order_id);

  if (order_iter == std::end(_all_orders))
    return false;


  auto order = std::move(order_iter->second);
  _all_orders.erase(order_iter);

  HalfOrderBook* hb = (order->side() == Side::buy)
                          ? &_books[order->symbol()].bids
                          : &_books[order->symbol()].asks;

  auto range = hb->equal_range(order->price());
  for (auto iter = range.first; iter != range.second; ++iter) {
    if (iter->second->client_order_id() == client_order_id) {
      hb->erase(iter);
      break;
    }
  }

  return true;
}

void MockMatchingEngine::cancel_order(const std::string& client_order_id)
{
  bool removed = remove_order(client_order_id);
  if (!removed) {
    throw MockRequestError(MockRequestError::ErrorCode::order_not_found,
                           "order not found");
  } else {
    // LOG_INFO("mock: order cancelled, id:" << client_order_id);
  }
}


void MockMatchingEngine::apply_trade(std::string symbol, double price,
                                     double size)
{
  assert(_event_loop.this_thread_is_ev());

  scope_guard on_done([this]() {
    if (!_completed_orders.empty())
      remove_completed_orders();
  });

  // we always want a tick to find or create a book, so that prices can be
  // available before a first order arrives
  auto& book = _books[symbol];
  book.market_data_ticking = true;

  double qty_remain = size;

  /*
  if (book.bids.size() > 0) {
    LOG_INFO("BEST BID: " << book.bids.begin()->first << ", last-price: "
                          << price << ", remain: " << qty_remain
                          << "isZero(): " << is_zero(qty_remain));
  }
   */

  for (auto iter = book.bids.rbegin();
       iter != book.bids.rend() && qty_remain > 0 && !is_zero(qty_remain);
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

      // apply the fill to the order
      const double qty_fill = std::min(qty_remain, order->size_remain());
      order->apply_fill(qty_fill);
      qty_remain -= qty_fill;

      bool order_fully_filled = is_zero(order->size_remain());
      if (order_fully_filled) {
        _completed_orders.push_back(order->client_order_id());
      }
    } else
      break; /* no prices left to fill */
  }

  for (auto iter = book.asks.begin();
       iter != book.asks.end() && qty_remain > 0 && !is_zero(qty_remain);
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
      if (order_fully_filled)
        _completed_orders.push_back(order->client_order_id());
    } else
      break; /* no prices left to fill */
  }
}

} // namespace apex
