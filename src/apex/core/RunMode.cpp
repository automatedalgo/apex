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

#include <apex/core/RunMode.hpp>

namespace apex
{

std::ostream& operator<<(std::ostream& os, RunMode m)
{
  os << to_string(m);
  return os;
}


RunMode parse_run_mode(std::string_view s)
{
  if (s == "paper")
    return RunMode::paper;
  if (s == "live")
    return RunMode::live;
  if (s == "backtest")
    return RunMode::backtest;

  std::string result("invalid RunMode value '");
  result += s;
  result += "'";
  throw std::runtime_error(result);
}


} // namepace
