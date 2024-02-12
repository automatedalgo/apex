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

#include <apex/model/Order.hpp>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>


namespace apex
{

class MockLimitOrder;
class RealtimeEventLoop;


class MockRequestError : public std::runtime_error
{
public:
  enum class ErrorCode {
    order_not_found,
    lot_size,
  };

  MockRequestError(ErrorCode code, const std::string& text)
    : std::runtime_error(text), code(code)
  {
  }

  ErrorCode code;
};

class MockMatchingEngine
{
public:
  explicit MockMatchingEngine(RealtimeEventLoop&);

  using OnFillFn = std::function<void(double size, bool fully_filled)>;
  using OnUnsolCancelFn = std::function<void()>;

  std::string add_order(const std::string& symbol, std::string client_order_id,
                        double size, double price, Side side,
                        OnFillFn on_fill_callback,
                        OnUnsolCancelFn on_unsol_cancel_fn);

  void cancel_order(const std::string& client_order_id);

  void apply_trade(std::string symbol, double price, double size);


private:
  void remove_completed_orders();

  bool remove_order(const std::string& client_order_id);

  RealtimeEventLoop& _event_loop;

  std::map<std::string, std::shared_ptr<MockLimitOrder>> _all_orders;

  using HalfOrderBook = std::multimap<double, std::shared_ptr<MockLimitOrder>>;
  struct OrderBook {
    HalfOrderBook bids;
    HalfOrderBook asks;

    // Could make this more sophisticated, eg, dedicate stale market data
    // etc.
    bool market_data_ticking = false;
  };
  std::map<std::string, OrderBook> _books;
  std::list<std::string> _completed_orders;
};

} // namespace apex
