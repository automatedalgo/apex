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

  struct Level {
    double price = nan;
    double qty = nan;
  };

  [[nodiscard]] bool is_valid() const {
    // TODO: also check vector size
    return !std::isnan(_bids[0].price) && !std::isnan(_asks[0].price);
  }
 
  void apply(TickBookSnapshot5&);

private:
  std::vector<Level> _bids;
  std::vector<Level> _asks;
};


class MarketData
{

public:
  struct EventType {

    EventType() = default;
    explicit EventType(int flags) : value(flags) {}

    enum Flag {
      trade = 0x01,
      top = 0x02,
      full_book = 0x04
    };

    int value;

    [[nodiscard]] bool is_trade() const { return value & Flag::trade; }
    [[nodiscard]] bool is_top() const { return value & Flag::top; }
  };


public:
  MarketData();

  void apply(TickTrade&);
  void apply(TickTop&);
  void apply(TickBookSnapshot5&);

  void subscribe_events(std::function<void(EventType)>);

  [[nodiscard]] bool has_last() const { return _last.is_valid(); }

  [[nodiscard]] bool is_crossed() const { return _l1_bid.price >= _l1_ask.price; }

  [[nodiscard]] double bid() const { return _l1_bid.price; }

  [[nodiscard]] double ask() const { return _l1_ask.price; };

  [[nodiscard]] double mid() const;

  [[nodiscard]] const TickTrade& last() const { return _last; }


  [[nodiscard]] double is_good() const {
    return (bid() != 0.0) &&
           (ask() != 0.0) &&
           (!is_crossed()) &&
           has_last();
  }

  [[nodiscard]] bool has_bid_ask() const { return (bid() != 0.0) &&
                                                  (ask() != 0.0) &&
                                                  (!is_crossed()); }
private:
  TickTrade _last;
  Book _book;
  Book::Level _l1_bid;
  Book::Level _l1_ask;

  std::vector<std::function<void(EventType)>> _events_listeners;
};

} // namespace apex
