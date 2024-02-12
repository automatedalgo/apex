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

#include <string>
#include <vector>

namespace apex
{

class Services;
class Instrument;

struct RestoredPosition {
  std::string strategy_id;
  std::string exchange;
  std::string native_symbol;
  double qty;
};

class PersistenceService
{
public:
  explicit PersistenceService(Services* services);

  // position actions
  void persist_instrument_positions(std::string algo_id,
                                    const Instrument& instrument, double qty);

  std::vector<RestoredPosition> restore_instrument_positions(
      std::string strategy_id);

private:
  Services* _services;
  std::string _persist_path;
};

} // namespace apex
