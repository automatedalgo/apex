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
#include <apex/model/Instrument.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/util/BacktestEventLoop.hpp>

#include <string>
#include <filesystem>
#include <list>

namespace apex
{

class TardisFileReader;
class MarketData;

enum class TickFormat {
  tickbin1 = 1,
  tardis
};
const char* to_string(TickFormat);

class BaseTickFileReader {
public:
  virtual void wind_forward(apex::Time t) = 0;
  [[nodiscard]] virtual bool has_next_event() const = 0;
  [[nodiscard]] virtual apex::Time next_event_time() const = 0;
  virtual void consume_next_event() = 0;
  virtual ~BaseTickFileReader() = default;
};

/* Find tick-files for according to criteria: exchange, instrument, data-type and
 * stream-type; and that are between the backtest time range. */
class TickReplayer : public BacktestEventSource
{
public:
  TickReplayer(const std::filesystem::path& tick_dir,
               TickFormat tick_format,
               const Instrument& instrument,
               MarketData*,
               MdStream stream,
               Time replay_from,
               std::list<Time> dates);

  ~TickReplayer() override;

  Time get_next_event_time() override;
  void consume_next_event() override;
  void init_backtest_time_range(Time /*start*/, Time /*end*/) override {}

  [[nodiscard]] size_t file_count() const { return _filenames.size(); }

private:
  std::tuple<std::list<std::filesystem::path>,
             std::list<std::filesystem::path>> find_tick_files();

  void get_next_file_reader();

  void build_tick_file_options();

  TickFormat _tick_format;
  Instrument _instrument;
  MarketData* _mktdata;
  MdStream _stream;
  Time _replay_from;
  std::list<Time> _dates;
  std::filesystem::path _base_dir;
  std::list<std::filesystem::path> _filenames;
  std::unique_ptr<BaseTickFileReader> _reader;

  // settings related to the specific tick-file format
  std::string _tick_subdir;
  std::function<std::unique_ptr<BaseTickFileReader>(std::filesystem::path)>  _tick_reader_factory;
  std::function<std::filesystem::path(Time)> _tick_filename_factory;
};

} // namespace
