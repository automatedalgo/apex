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

#include <apex/util/Time.hpp>
#include <apex/util/json.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/model/ExchangeId.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/core/OrderRouter.hpp>

#include <filesystem>

namespace apex
{


class Services;
class Instrument;
class MarketData;
class BacktestService;
class Instrument;
class SimLimitOrder;
class SimOrderBook;


class SimExchange : public OrderRouter
{
public:
  SimExchange(Services*);

  ~SimExchange() override;

  void send_order(Order&) override;
  void cancel_order(Order&) override;
  bool is_up() const override;

  void add_instrument(const Instrument&);

private:
  using ExtOrderId = std::string;
  Services* _services;
  std::map<ExtOrderId, std::shared_ptr<SimLimitOrder>> _all_orders;
  std::map<Instrument, std::unique_ptr<SimOrderBook>> _books;
};



} // namespace apex
