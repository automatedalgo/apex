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

#include <map>
#include <memory>

namespace apex
{

class Services;
class Instrument;
class MarketData;

class MarketDataService
{
public:
  explicit MarketDataService(Services*);
  ~MarketDataService();

  /* Find/create a MarketData instance for instrument, or else null if the
   * MarketData cannot be created (eg due to no suitable session). */
  MarketData* find_market_data(const Instrument&);

private:
  Services* _services;
  std::map<Instrument, std::unique_ptr<MarketData>> _markets;
};

} // namespace apex
