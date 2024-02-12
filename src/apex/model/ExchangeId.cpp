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

#include <apex/model/ExchangeId.hpp>

#include <apex/util/Error.hpp>

#include <sstream>

namespace apex
{

ExchangeId to_exchange_id(const std::string& s)
{
  if (s == "binance")
    return apex::ExchangeId::binance;
  else if (s == "binance_usdfut")
    return apex::ExchangeId::binance_usdfut;
  else if (s == "binance_coinfut")
    return apex::ExchangeId::binance_coinfut;
  else {
    THROW("not a valid exchange type: '" << s << "'");
  }
}


const char* exchange_id_to_string(ExchangeId e) {
  switch (e) {
    case ExchangeId::none:
      return "none";
    case ExchangeId::binance:
      return "binance";
    case ExchangeId::binance_usdfut:
      return "binance_usdfut";
    case ExchangeId::binance_coinfut:
      return "binance_coinfut";
    default:
      THROW("invalid exchange ID");
  }
}

std::ostream& operator<<(std::ostream& os, ExchangeId e)
{
  os << exchange_id_to_string(e);
  return os;
}


}
