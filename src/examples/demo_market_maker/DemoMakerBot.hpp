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

#include <apex/core/Bot.hpp>


namespace apex
{

struct OrderExtraInfo {
  double cancel_price = 0.0;
};

class Strategy;

class DemoMakerBot : public apex::Bot
{
  double _target_position_usd = 1050;

public:
  DemoMakerBot(Strategy*, apex::Instrument);

  void on_order_closed(apex::Order& order) override;

  void on_tick_trade(apex::MarketData::EventType) override;


private:
  void manage_pending_orders();


  void manage_live_orders();
  void manage_order_initiation();


  void on_timer() override;
};

}
