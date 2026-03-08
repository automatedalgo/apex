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

#include <apex/core/OrderRouter.hpp>
#include <apex/model/Instrument.hpp>

#include <map>

namespace apex
{

class Core;
class OrderRouter;
class Instrument;
class SimExchange;


struct OrderRouterConfig {
  std::string api_key_file;
};

/*
  Purpose of the OrderRouterService is to provide route to market (an
  OrderRouter instance) for each Instrument the engine trades.
 */
class OrderRouterService
{
public:
  explicit OrderRouterService(Core*);
  ~OrderRouterService();

  /* Get an OrderRouter object for sending orders to the provided exchange, and
   * this is configured with the provided strategy_id. */
  OrderRouter* get_order_router(Instrument&,
                                const std::string& strategy_id);

  // Add a generic route, from external config
  void add_route();

  // add Binance USD Futures route
  void add_binance_usdfut(OrderRouterConfig);

  // add Binance Spot route
  void add_binance_spot(OrderRouterConfig);


private:
  Core* _core;

  // order router services for live trading
  std::map<ExchangeId, std::shared_ptr<OrderRouter>> _routers;

  // order routers for simulation (paper-trading & backtest)
  std::map<ExchangeId, std::unique_ptr<SimExchange>> _sim_exchanges;
};

}
