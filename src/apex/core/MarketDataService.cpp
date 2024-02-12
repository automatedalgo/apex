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

#include <apex/comm/GxClientSession.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/RefDataService.hpp>
#include <apex/core/BacktestService.hpp>
#include <apex/core/Services.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/MarketData.hpp>

namespace apex
{

MarketDataService::MarketDataService(Services* services) :
  _services(services)
{
}


MarketDataService::~MarketDataService() {}


MarketData* MarketDataService::find_market_data(const Instrument& instrument)
{
  auto iter = _markets.find(instrument);
  if (iter != std::end(_markets))
    return iter->second.get();

  // TODO: when creating the market-data object, need to decide on the stream
  // configuration.
  MdStreamParams stream_params;
  stream_params.mask |= static_cast<int>(MdStream::AggTrades);
  stream_params.mask |= static_cast<int>(MdStream::L1);

  auto mkt = std::make_unique<MarketData>();
  MarketData* mv = mkt.get();

  if (_services->is_backtest()) {
    auto backtest_svc = _services->backtest_service();
    backtest_svc->subscribe_canned_data(instrument, mkt.get(), stream_params);
  }
  else {
    auto session = _services->gateway_service()->find_session(instrument.exchange_id());

    if (!session)
      return {};

    LOG_INFO("subscribing to market data for " << instrument
             << " (object: "<< mv<< ")");
    session->subscribe(instrument.native_symbol(), instrument.exchange_id(), mv);
  }


  _markets.insert({instrument, std::move(mkt)});
  return mv;
}

} // namespace apex
