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

#include <apex/model/Instrument.hpp>

#include <apex/util/Error.hpp>

namespace apex
{

const char * instrument_type_to_string(InstrumentType t) {

  switch (t) {
    case InstrumentType::coinpair:
      return "coinpair";
    case InstrumentType::perpetual:
      return "perp";
    case InstrumentType::future:
      return "future";
    case InstrumentType::none:
      return "none";
    default:
      THROW("invalid instrument-type");
  }
}


InstrumentType to_instrument_type(const std::string& s)
{
  if (s == "coinpair")
    return InstrumentType::coinpair;
  else if (s == "perp")
    return InstrumentType::perpetual;
  else if (s == "future")
    return InstrumentType::future;
  else
    THROW("not a valid instrument-type: '" << s << "'");
}


bool Instrument::operator==(const Instrument& other) const
{
  return (other.lot_size == lot_size) &&
         (other.minimum_size == minimum_size) &&
         (other.minimum_notnl == minimum_notnl) &&
         (other.tick_size == tick_size) & (other._type == _type) &&
         (other._base == _base) && (other._quote == _quote) &&
         (other._symbol == _symbol);
}


bool Instrument::operator<(const Instrument& other) const
{
  return (_exchange_id < other._exchange_id) ||
         (_exchange_id == other.exchange_id() && _symbol < other._symbol);
}

std::ostream& operator<<(std::ostream& os, const Asset& asset)
{
  os << asset.symbol();
  return os;
}

std::ostream& operator<<(std::ostream& os, const Instrument& instrument)
{
  os << instrument.id();
  return os;
}

} // namespace apex
