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

#include <apex/backtest/TickReplayer.hpp>
#include <apex/core/BacktestService.hpp>
#include <apex/core/Logger.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/core/Services.hpp>

#include <apex/model/Instrument.hpp>
#include <apex/util/BacktestEventLoop.hpp>
#include <apex/util/Error.hpp>

namespace apex
{


std::list<apex::Time> get_dates_in_range(apex::Time from, apex::Time upto)
{
  const std::chrono::hours one_day{24};

  auto day_upto = upto.round_to_earliest_day();
  day_upto += one_day;

  auto day_from = from.round_to_earliest_day();

  if (from >= upto)
    throw ConfigError("backtest from-time must be before upto-time");

  std::list<apex::Time> dates;
  auto day = day_from;
  while (day < day_upto) {
    dates.push_back(day);
    day += one_day;
  }

  return dates;
}

BacktestService::BacktestService(Services* services, apex::Time replay_from,
                                 apex::Time replay_upto)
  : _services(services),
    _from(replay_from),
    _upto(replay_upto),
    _dates{get_dates_in_range(_from, _upto)}
{
  LOG_INFO("number of backtest dates: " << _dates.size());
}


BacktestService::~BacktestService() = default;


void BacktestService::create_tick_replayer(const Instrument& instrument,
                                           MarketData* mktdata,
                                           MdStream stream_type)
{

  std::string tick_format = "tardis";
  std::string base_directory = _services->paths_config().tickdata / "tardis";

  std::pair<Instrument, MdStream> key{instrument, stream_type};

  auto sp = std::make_unique<TickReplayer>(base_directory,
                                           tick_format,
                                           instrument, mktdata, stream_type,
                                           _from, _upto, _dates);

  auto file_count = sp->file_count();
  if (!file_count) {
    THROW("no tick-data files found for stream "
          << instrument << "/" << stream_type
          << " for dates "
          << _dates.front().strftime("%Y/%m/%d") << " - "
          << _dates.back().strftime("%Y/%m/%d")
          << ", looking under path '" << base_directory << "'"
      );
  }

  _services->backtest_evloop()->add_event_source(sp.get());

  LOG_INFO("tick-data files found for '"
           << instrument << "/" << stream_type <<"' : " << file_count);
  _replayers.insert({key, std::move(sp)});
}


void BacktestService::subscribe_canned_data(const Instrument& instrument,
                                            MarketData* mktdata,
                                            MdStreamParams stream_params)
{
  if (stream_params.mask == 0) {
    THROW("no market-data streams configured when subscribing to " << instrument);
  }

  // To resolve the market data subscribe, we need information that tells us
  // which set of tick-files to use, and which decoder to use.  This information
  // will come from the application

  if (stream_params.mask & static_cast<int>(MdStream::AggTrades))
    create_tick_replayer(instrument, mktdata, MdStream::AggTrades);

  if (stream_params.mask & static_cast<int>(MdStream::L1))
    create_tick_replayer(instrument, mktdata, MdStream::L1);
}

} // namespace apex
