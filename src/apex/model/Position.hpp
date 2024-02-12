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
  // position loaded from external persistence at startup
  double _startup;

  // accumulated traded quantity
  double _traded_long;
  double _traded_short;

  // live positions, not yet traded; used to arrive theo positions
  double _live_long;
  double _live_short;

public:
  explicit Position(double startup = 0.0);

  [[nodiscard]] double net() const;

  void apply_fill(Side, double qty);
};

} // namespace apex
