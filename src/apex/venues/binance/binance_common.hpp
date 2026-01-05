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
#include <apex/model/tick_msgs.hpp>
#include <simdjson/simdjson.h>

namespace apex
{
Side buyer_market_maker_to_aggrSide(bool buyer_is_maker);

Time from_binance_timestamp(unsigned long i);

std::string to_binance(TimeInForce tif);

const char* to_binance(Side s);

std::pair<TickTrade, int> parse_binanceusdfut_aggtrade(simdjson::ondemand::object&);
std::pair<TickTop, int> parse_binanceusdfut_bookticker(simdjson::ondemand::object&);

}
