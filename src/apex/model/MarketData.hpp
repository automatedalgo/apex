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

#include <apex/model/tick_msgs.hpp>

#include <functional>
#include <cmath>

namespace apex
{

enum class MdStream : int {
  Null = 0,
  L1 = 1 << 0,        // top bid/ask
  L2 = 1 << 1,        // partial book, default depth
  L3 = 1 << 2,        // full depth
  Trades = 1 << 10,   // individual trades
  AggTrades = 1 << 11 // trades aggregated at price
};

std::ostream& operator<<(std::ostream&, MdStream&);

struct MdStreamParams
{
  int mask = 0;
};


class Book
{
public:
  double best_bid_price = nan;
  double best_ask_price = nan;

  [[nodiscard]] bool is_valid() const {
    return !std::isnan(best_bid_price) && !std::isnan(best_ask_price) ;
  }
};


class MarketData
{

public:
  struct EventType {
    enum Flag {
      trade = 0x01,
      top = 0x02
    };

    int value;

    [[nodiscard]] bool is_trade() const { return value & Flag::trade; }
    [[nodiscard]] bool is_top() const { return value & Flag::top; }
  };


public:
  MarketData();

  [[nodiscard]] const TickTrade& last() const { return _last; }
  Book& book() { return _book; }

  void apply(TickTrade&);
  void apply(TickTop&);
  void subscribe_events(std::function<void(EventType)>);

  [[nodiscard]] bool has_last() const { return _last.is_valid(); }
  [[nodiscard]] bool has_bid_ask() const { return _book.is_valid(); }

private:
  TickTrade _last;
  Book _book;
  std::vector<std::function<void(EventType)>> _events_listeners;
};

} // namespace apex
