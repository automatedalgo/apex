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
#include <apex/model/MarketData.hpp>
#include <apex/core/OrderRouter.hpp>

#include <list>
#include <memory>
#include <map>

namespace apex
{

class TickReplayer;

class BacktestService
{
public:
  BacktestService(Core*, apex::Time replay_from, apex::Time replay_upto);
  ~BacktestService();
  void subscribe_canned_data(const Instrument&, MarketData*, MdStreamParams stream_params);

private:

  void create_tick_replayer(const Instrument& instrument,
                            MarketData* mktdata,
                            MdStream stream_type);

  Core* _core;
  apex::Time _from;
  apex::Time _upto;
  std::list<Time> _dates;

  std::map<std::pair<Instrument, MdStream>,
           std::unique_ptr<TickReplayer>> _replayers;
};


} // namespace apex
