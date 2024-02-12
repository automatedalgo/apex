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

#include <apex/util/utils.hpp>
#include <apex/model/ExchangeId.hpp>

namespace apex
{

enum class InstrumentType : int { none = 0, coinpair, perpetual, future };

InstrumentType to_instrument_type(const std::string& s);
const char * instrument_type_to_string(InstrumentType t);

struct Asset {

public:
  Asset() : _precision(0) {}

  Asset(std::string symbol, const std::string& exchange, int precision)
    : _symbol(std::move(symbol)),
      _exchange_id(to_exchange_id(exchange)),
      _precision(precision)
  {
  }

  Asset(const Asset&) = default;

  bool operator==(const Asset& other) const
  {
    return (other._symbol == this->_symbol) &&
           (other._exchange_id == this->_exchange_id) &&
           (other._precision == this->_precision);
  }

  bool operator<(const Asset& other) const
  {
    return (this->_exchange_id < other._exchange_id) ||
           (this->_exchange_id == other._exchange_id && this->_symbol < other._symbol);
  }

  const std::string & symbol() const { return _symbol; }

  // TODO: add AssetType?  eg, fiat, coin, future
private:
  std::string _symbol;   // native symbol on exchange
  ExchangeId _exchange_id;
  int _precision;
};


/* Represent a financial product on an exchange that can be bought & sold.  The
 * instrument will be priced in the quote Asset currency. */
struct Instrument {

  ScaledInt lot_size;
  double minimum_size;
  double minimum_notnl;
  ScaledInt tick_size;

  Instrument(InstrumentType type, std::string inst_id, Asset base, Asset quote,
             std::string native_symbol, std::string venue)
    : _type(type),
      _id(std::move(inst_id)),
      _base(base),
      _quote(quote),
      _symbol(native_symbol),
      _venue(venue),
      _exchange_id(to_exchange_id(venue))
  {
  }

  Instrument(const Instrument& i) = default;

  [[nodiscard]] std::string exchange_name() const { return exchange_id_to_string(_exchange_id); }
  [[nodiscard]] ExchangeId exchange_id() const { return _exchange_id; }

  [[nodiscard]] const std::string& native_symbol() const { return _symbol; }

  [[nodiscard]] const Asset& base() const { return _base; }
  [[nodiscard]] const Asset& quote() const { return _quote; }

  [[nodiscard]] InstrumentType type() const { return _type; }

  bool operator==(const Instrument& other) const;
  bool operator<(const Instrument& other) const;

  [[nodiscard]] bool empty() const { return _symbol.empty(); }

  [[nodiscard]] const std::string & symbol() const { return _symbol; }

 [[nodiscard]] const std::string& id() const { return _id; }

private:
  InstrumentType _type;
  std::string _id;
  Asset _base;
  Asset _quote;
  std::string _symbol;
  std::string _venue;
  ExchangeId _exchange_id;
};

std::ostream& operator<<(std::ostream&, const Instrument&);


}
