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

#include <apex/venues/binance/binance_common.hpp>

namespace apex
{

Side buyer_market_maker_to_aggrSide(bool buyer_is_maker)
{
  if (buyer_is_maker)
    return Side::sell;
  else
    return Side::buy;
}


Time from_binance_timestamp(json::number_unsigned_t i)
{
  int ms = i % 1000;
  int sec = (i - ms) / 1000;
  return Time{sec, std::chrono::milliseconds(ms)};
}


const char* to_binance(Side s)
{
  switch (s) {
    case Side::buy:
      return "BUY";
    case Side::sell:
      return "SELL";
    default:
      throw std::runtime_error("invalid Side");
  }
}

std::string to_binance(TimeInForce tif)
{
  switch (tif) {
    case TimeInForce::gtc:
      return "GTC";
    case TimeInForce::ioc:
      return "IOC";
    case TimeInForce::fok:
      return "FOK";
    default:
      return "?";
  }
}

}
