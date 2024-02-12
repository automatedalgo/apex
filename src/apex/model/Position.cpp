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

#include <apex/model/Position.hpp>
#include <apex/core/Logger.hpp>

namespace apex
{

Position::Position(double startup)
  : _startup(startup),
    _traded_long(0),
    _traded_short(0),
    _live_long(0),
    _live_short(0)
{
}


double Position::net() const { return _startup + _traded_long - _traded_short; }

void Position::apply_fill(Side s, double qty)
{
  if (s == Side::buy) {
    _traded_long += qty;
  } else if (s == Side::sell) {
    _traded_short += qty;
  } else {
    LOG_ERROR("cannot apply fill to position, because side not set");
  }
}

} // namespace apex
