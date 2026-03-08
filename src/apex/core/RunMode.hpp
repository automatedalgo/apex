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

#include <ostream>
#include <string>

namespace apex
{

enum class RunMode {
  paper = 1,     // paper trading
  live = 2,      // live trading
  backtest = 3   // backtest
};

inline const char* to_string(RunMode m)
{
  switch (m) {
    case RunMode::paper:
      return "paper";
    case RunMode::live:
      return "live";
    case RunMode::backtest:
      return "backtest";
    default:
      return "invalid";
  }
}

std::ostream& operator<<(std::ostream&, RunMode);
RunMode parse_run_mode(std::string_view s);

} // namepace
