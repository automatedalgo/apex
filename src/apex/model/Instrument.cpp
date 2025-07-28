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

Instrument::Instrument(InstrumentType type,
                       std::string inst_id,
                       Asset base,
                       Asset quote,
                       std::string native_symbol,
                       std::string feed_symbol,
                       std::string line_symbol,
                       std::string venue)
    : _type(type),
      _id(std::move(inst_id)),
      _base(base),
      _quote(quote),
      _symbol(native_symbol),
      _feed_symbol(feed_symbol),
      _line_symbol(line_symbol),
      _venue(venue),
      _exchange_id(to_exchange_id(venue)),
      _minimum_notnl(0),
      _has_min_notl(false)
  {
  }

void Instrument::set_minimum_notnl(double d) {
  _minimum_notnl = d;
  _has_min_notl = isfinite(d);
}

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


bool Instrument::operator==(const Instrument& rhs) const
{
  // TODO: also check all the other fields, but, becware, some are can be nan
  return
    (rhs._type == _type) &&
    (rhs._id == _id) &&
    (rhs._base == _base) &&
    (rhs._quote == _quote) &&
    (rhs._symbol == _symbol) &&
    (rhs._venue == _venue) &&
    (rhs._exchange_id == _exchange_id);
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
