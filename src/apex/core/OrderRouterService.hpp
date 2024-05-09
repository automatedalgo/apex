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

class Services;
class OrderRouter;
class Instrument;
class SimExchange;

class OrderRouterService
{
public:
  explicit OrderRouterService(Services*);
  ~OrderRouterService();
  /* Get an OrderRouter object for sending orders to the provided exchange, and
   * this is configured with the provided strategy_id. */
  OrderRouter* get_order_router(Instrument&,
                                const std::string& strategy_id);

private:
  Services* _services;

  // order router services for live trading
  std::map<std::pair<ExchangeId, std::string>, std::unique_ptr<OrderRouter>> _routers;

  // order routers for simulation (paper-trading & backtest)
  std::map<ExchangeId, std::unique_ptr<SimExchange>> _sim_exchanges;
};

}
