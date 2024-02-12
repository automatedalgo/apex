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

#include <apex/model/Instrument.hpp>
#include <map>
#include <string>

namespace apex
{

class Services;
class Config;


std::ostream& operator<<(std::ostream& os, const Asset& asset);
std::ostream& operator<<(std::ostream&, const Instrument&);

struct InstrumentQuery {
  std::string symbol;
  std::string exchange;
  ExchangeId exchange_id = ExchangeId::none;
  InstrumentType type = InstrumentType::none;

  explicit InstrumentQuery(std::string query_symbol) : symbol(std::move(query_symbol)) {}

  InstrumentQuery(std::string query_symbol, InstrumentType query_type)
    : symbol(std::move(query_symbol)), type(query_type)
  {
  }


  InstrumentQuery(std::string query_symbol, ExchangeId exchange)
    : symbol(std::move(query_symbol)), exchange_id(exchange)

  {
  }

  InstrumentQuery(std::string query_symbol, std::string query_exchange)
    : symbol(std::move(query_symbol)), exchange(std::move(query_exchange))

  {
  }

  InstrumentQuery(std::string query_symbol,
                  std::string query_exchange,
                  InstrumentType query_type)
    : symbol(std::move(query_symbol)), exchange(std::move(query_exchange)), type(query_type)
  {
  }

  std::string to_string() const;
};

class RefDataService
{
public:
  RefDataService(Services*, Config);

  Asset& get_asset(const std::string& symbol);

  std::vector<Instrument> get_fx_rate_instruments(const Instrument&);

  Instrument& get_instrument(
    const std::string& symbol, InstrumentType type);

  Instrument& get_instrument(
    const std::string& symbol, const std::string& exchange = {},
    InstrumentType type = InstrumentType::none);

  Instrument& get_instrument(struct InstrumentQuery);

  [[nodiscard]] bool is_fx_rate_instrument(const Instrument&) const;

private:
  void load_assets(const std::string& filename);
  Asset& find_or_create_asset(const std::string& venue,
                              const std::string& symbol,
                              const std::string& precision);

  Services* _services;

  std::map<std::string, Instrument> _instruments;
  std::map<std::string, Asset> _assets;
  std::vector<Asset> _notional_ccy_assets;
};

} // namespace apex
