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


Time from_binance_timestamp(unsigned long i)
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


std::pair<TickTrade, int> parse_binanceusdfut_aggtrade(simdjson::ondemand::object& msg)
{
  std::pair<TickTrade, int> rv{};
  std::string tmp;

  for (auto field : msg) {
    char c = field.key()[0];
    switch (c) {
      case 'q':  {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.qty = std::stod(tmp);
        break;
      }
      case 'p': {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.price = std::stod(tmp);
        break;
      }
      case 'm': {
        bool v{false};
        rv.second |= (field.value().get(v) != simdjson::error_code::SUCCESS);
        rv.first.side = buyer_market_maker_to_aggrSide(v);
        break;
      }
      case 'T': {
        uint64_t v{};
        rv.second |= (field.value().get(v) != simdjson::error_code::SUCCESS);
        rv.first.xt = from_binance_timestamp(v);
        break;
      }
      case 'E': {
        uint64_t v{};
        rv.second |= (field.value().get(v) != simdjson::error_code::SUCCESS);
        rv.first.et = from_binance_timestamp(v);
        break;
      }
    }
  }

  return rv;
}


std::pair<TickTop, int> parse_binanceusdfut_bookticker(simdjson::ondemand::object& msg)
{
  std::pair<TickTop, int> rv{};
  std::string tmp;

  for (auto field : msg) {
    char c = field.key()[0];
    switch (c) {
      case 'a':  {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.ask_price = std::stod(tmp);
        break;
      }
      case 'A': {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.ask_qty = std::stod(tmp);
        break;
      }
      case 'b': {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.bid_price = std::stod(tmp);
        break;
      }
      case 'B': {
        rv.second |= (field.value().get(tmp) != simdjson::error_code::SUCCESS);
        rv.first.bid_qty = std::stod(tmp);
        break;
      }
      case 'T': {
        uint64_t v{};
        rv.second |= (field.value().get(v) != simdjson::error_code::SUCCESS);
        rv.first.xt = from_binance_timestamp(v);
        break;
      }
      case 'E': {
        uint64_t v{};
        rv.second |= (field.value().get(v) != simdjson::error_code::SUCCESS);
        rv.first.et = from_binance_timestamp(v);
        break;
      }
    }
  }

  return rv;
}


}
