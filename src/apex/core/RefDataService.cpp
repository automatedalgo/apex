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

#include <apex/core/Logger.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/Error.hpp>
#include <apex/util/utils.hpp>

#include <fast-cpp-csv-parser/csv.h>

#include <filesystem>
#include <set>

namespace apex
{

std::string InstrumentQuery::to_string() const {
  std::ostringstream oss;
  oss << "symbol:" << QUOTE(symbol);
  if (!exchange.empty())
    oss << ", exchange:" << QUOTE(exchange);
  if (type != InstrumentType::none)
    oss << ", type:" << instrument_type_to_string(type);
  return oss.str();
}

RefDataService::RefDataService(Services* services, Config config)
  : _services(services)
{
  auto default_path = _services->paths_config().refdata;
  default_path = default_path / "instruments" / "instruments.csv";

  auto filename = config.get_string("instruments_csv", default_path.string());
  try {
    load_assets(filename);
  }
  catch (std::exception& e) {
    LOG_ERROR("failed to initialse instrument ref-data: " << e.what());
    throw;
  }
}

Asset& RefDataService::find_or_create_asset(const std::string& venue,
                                            const std::string& symbol,
                                            const std::string& precision)
{
  auto iter = _assets.find(symbol);
  if (iter == std::end(_assets)) {
    Asset asset = Asset(symbol, venue, std::stoi(precision));
    iter = _assets.insert({symbol, asset}).first;
  }
  return iter->second;
}


Asset& RefDataService::get_asset(const std::string& symbol)
{
  auto iter = _assets.find(symbol);
  if (iter == std::end(_assets))
    THROW("no assets found for symbol '" << symbol << "'");

  return iter->second;
}

Instrument& RefDataService::get_instrument(const std::string& symbol,
                                           InstrumentType type)
{
  return get_instrument({symbol, "", type});
}


Instrument& RefDataService::get_instrument(InstrumentQuery q) {
  std::vector<std::string> matches;

  for (auto& pair : _instruments) {
    if ((pair.second.native_symbol() == q.symbol) &&
        (q.exchange_id == ExchangeId::none|| (pair.second.exchange_id() == q.exchange_id)) &&
        (q.type == InstrumentType::none || pair.second.type() == q.type)) {
      matches.push_back(pair.first);
    }
  }

  if (std::size(matches) == 1) {
    return _instruments.find(matches[0])->second;
  }
  else if (std::size(matches) > 1) {
    THROW("multiple instruments match query " << q.to_string());
  } else {
    THROW("instrument not found for query " << q.to_string());
  }

}

Instrument& RefDataService::get_instrument(const std::string& symbol,
                                           const std::string& exchange,
                                           InstrumentType type)
{
  return get_instrument({symbol, exchange, type});
}


bool RefDataService::is_fx_rate_instrument(const Instrument& instr) const
{
  for (auto& ccy : _notional_ccy_assets)
    if (instr.quote() == ccy)
      return true;
  return false;
}


std::vector<Instrument> RefDataService::get_fx_rate_instruments(
    const Instrument& i)
{
  // TODO: as we detect matches, should prefer
  // coinpair, and perp, then futures.  So need to have three
  // vectors, and then concat them at the end.
  std::vector<Instrument> matches;
  for (auto& ccy : _notional_ccy_assets) {
    for (auto& iter : _instruments) {
      if (iter.second.base() == i.quote() && iter.second.quote() == ccy)
        matches.push_back(iter.second);
    }
  }

  return matches;
}


void RefDataService::load_assets(const std::string& filename)
{
  LOG_INFO("reading ref-data csv file " << QUOTE(filename));
  io::CSVReader<12> in(filename);

  in.read_header(io::ignore_extra_column, "instId", "symbol", "type", "venue",
                 "baseAsset", "quoteAsset", "lotQty", "tickSize", "minNotional",
                 "minQty", "baseAssetPrecision", "quoteAssetPrecision");

  std::string instId, symbol, type, venue, baseAsset, quoteAsset, lotQty,
      tickSize, minNotional, minQty, baseAssetPrecision, quoteAssetPrecision;

  while (in.read_row(instId, symbol, type, venue, baseAsset, quoteAsset,
                     lotQty, tickSize, minNotional, minQty, baseAssetPrecision,
                     quoteAssetPrecision)) {
    auto iter = _instruments.find(instId);

    // create an Instrument object, even if already found
    {
      Asset& base = find_or_create_asset(venue, baseAsset, baseAssetPrecision);
      Asset& quote =
          find_or_create_asset(venue, quoteAsset, quoteAssetPrecision);
      apex::InstrumentType instrument_type = apex::to_instrument_type(type);
      Instrument instrument =
        Instrument(instrument_type, instId, base, quote, symbol, venue);
      instrument.minimum_size = std::atof(minQty.c_str());
      instrument.minimum_notnl = std::atof(minNotional.c_str());
      instrument.tick_size = ScaledInt(tickSize);
      instrument.lot_size = ScaledInt(lotQty.c_str());

      if (iter == std::end(_instruments)) {
        LOG_DEBUG("Added instrument " << instrument);
        _instruments.insert({instId, instrument});
      } else {
        if (iter->second == instrument) {
          LOG_WARN("skipping duplicate instrument " << QUOTE(symbol));
        } else {
          THROW("ref-data symbol defined twice " << QUOTE(symbol));
        }
      }
    }
  }
  LOG_INFO("refdata loaded, " << _assets.size() << " assets, "
                              << _instruments.size() << " instruments");

  // Define the notionl ccy and assets that are proxies
  std::string notional_ccy = "USD";
  std::string ccy_proxies[] = {"USDT", "BUSD", "USD"};


  // Find a USD equivalent asset
  for (auto ccy : ccy_proxies) {
    auto iter = _assets.find(ccy);
    if (iter != std::end(_assets)) {
      _notional_ccy_assets.push_back(iter->second);
      LOG_INFO("FX-rate asset:" << iter->second);
    }
  }
  if (_notional_ccy_assets.empty())
    LOG_WARN("no " << notional_ccy << " equivalent assets found");
}


} // namespace apex
