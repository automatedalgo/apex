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

namespace apex
{

class Position
{
public:
  explicit Position(double startup = 0.0);

  double net_qty() const {
    return _startup + _buy_qty - _sell_qty;
  }

  // Quantity bought, in base asset units
  double buy_qty() const { return _buy_qty; }

  // Quantity sold, in base asset units
  double sell_qty() const { return _sell_qty; }

  // Cost of all buys, in asset currency
  double buy_cost() const { return _buy_cost; }

  // Cost of all sells, in asset currency
  double sell_cost() const { return _sell_cost; }

  // Total turnover traded, in asset currency
  double total_turnover(double mark_price) const {
    return (_sell_cost + _buy_cost)
      + abs((_buy_qty - _sell_qty)) * mark_price;
  }

  double total_pnl(double mark_price) const {
    return (_sell_cost - _buy_cost)
      + (_buy_qty - _sell_qty) * mark_price;
  }

  void apply_fill(Side side, double qty, double price)
  {
    if (side == Side::buy) {
      _buy_qty += qty;
      _buy_cost += qty*price;
    }
    else if (side == Side::sell) {
      _sell_qty += qty;
      _sell_cost += qty*price;
    }
  }

private:

  // position loaded from external persistence at startup
  double _startup;

  // total quantities bought & sold, during current session
  double _buy_qty;
  double _sell_qty;

  // total value bought & sold, during current session, in the asset ccy
  double _buy_cost;
  double _sell_cost;
};

} // namespace apex
