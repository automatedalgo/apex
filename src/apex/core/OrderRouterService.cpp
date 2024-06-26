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

#include <apex/core/BacktestService.hpp>
#include <apex/core/GatewayService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/OrderRouterService.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/Error.hpp>
#include <apex/backtest/SimExchange.hpp>

#include <memory>
#include <cassert>

namespace apex
{

OrderRouterService::OrderRouterService(Services* services) :
  _services(services)
{
}

OrderRouterService::~OrderRouterService() {
}

/* Get an OrderRouter object for sending orders to the provided exchange, and
 * this is configured with the provided strategy_id. */
OrderRouter* OrderRouterService::get_order_router(Instrument& instrument,
                                                  const std::string& strategy_id)
{

  if (_services->run_mode() == RunMode::paper || _services->run_mode() == RunMode::backtest) {
    // create/obtain an exchange simulator
    auto iter = _sim_exchanges.find(instrument.exchange_id());
    if (iter == std::end(_sim_exchanges)) {
      bool inserted = false;
      std::tie(iter, inserted) = _sim_exchanges.insert({instrument.exchange_id(),
                                                        std::make_unique<SimExchange>(_services)});
      if (inserted) {
        LOG_INFO("created exchange-simulator for " << QUOTE(instrument.exchange_name()));
      }
    }

    // add our instrument to the exchange simulator
    iter->second->add_instrument(instrument);
    return iter->second.get();
  }
  else {
    auto exchange = instrument.exchange_id();
    auto key = std::make_pair(exchange, strategy_id);

    if ( auto iter = _routers.find(key);iter != std::end(_routers))
      return iter->second.get();

    LOG_NOTICE("creating OrderRouter for exchange " << QUOTE(exchange));

    auto gx_session = _services->gateway_service()->find_session(exchange);
    if (!gx_session) {
      THROW("cannot find GxSession for exchange " << QUOTE(exchange));
    }
    auto router = std::make_unique<RealtimeOrderRouter>(_services,
                                                        std::move(gx_session), strategy_id);

    auto inserted = _routers.insert({key, std::move(router)});

    assert(inserted.first->second.get() != nullptr);
    return inserted.first->second.get();
  }
}


}
