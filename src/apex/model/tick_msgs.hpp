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
#include <apex/util/Time.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace apex
{

enum class TradeType { null = 0, single, aggr };

static const double nan = std::numeric_limits<double>::quiet_NaN();


struct PxHeader {
  enum SymbolType {pxsym=1, ticker=2} source;
  char symbol[32];
};

struct TickTrade {
  double price = nan;
  double qty = 0;
  Side side = Side::none;
  TradeType type = TradeType::null;
  Time xt = {};  // execution time
  Time et = {};  // event published

  [[nodiscard]] bool is_valid() const { return !std::isnan(price); }
};

// Represent a change to both sides of Level1 market data.
struct TickTop {
  double bid_price;
  double bid_qty;
  double ask_price;
  double ask_qty;
  Time xt;
  Time et;
};


struct TickBookLevel
{
  double bid_price;
  double bid_qty;
  double ask_price;
  double ask_qty;
};


struct TickBookSnapshot5
{
  static constexpr int N = 5;

  Time xt = {};
  Time et = {};
  std::array<TickBookLevel, N> levels;
};

struct TickBookSnapshot25
{
  Time xt = {};
  Time et = {};
  std::array<TickBookLevel, 25> levels;
};

// logging utilities
std::ostream& operator<<(std::ostream&, const TickTrade&);

} // namespace apex
