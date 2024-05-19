/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free software: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (
// at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <apex/util/Time.hpp>
#include <fstream>

namespace apex
{

class MarketData;
class Position;
class OrderEvent;
class Services;

// Responsible for capturing detailed transaction records of the trading
// strategy, including order activity, fills, pnl etc, and either directly
// building detailed finance report, or, publishing the raw transaction data to
// external auditor.  This differs from application monitoring, in that the
// Auditor is only interested in financial data.
class Auditor
{
public:

  Auditor(Services*);
  ~Auditor();

  void add_transaction(Time event_time,
                       const std::string& strat_id,
                       const OrderEvent& order_event,
                       const std::string event_type,
                       const Position& position,
                       const MarketData* market_data,
                       double fx_to_usd,
                       bool is_fill,
                       double fill_qty,
                       double fill_price);

private:
  Services* _services;
  std::ofstream _file;
};

}
