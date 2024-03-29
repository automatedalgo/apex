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

#include <apex/backtest/TickFileReader.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/core/MarketDataService.hpp>

namespace apex
{

TickReplayer::TickReplayer(std::string base_directory,
                           const Instrument& instrument, MarketData* mktdata,
                           MdStream stream, Time replay_from,
                           Time replay_upto, std::list<Time> dates)
  : _instrument(instrument),
    _mktdata(mktdata),
    _stream(stream),
    _replay_from(replay_from),
    _replay_upto(replay_upto),
    _dates(dates)
{
  _base_dir = base_directory;
  _base_dir /= instrument.exchange_name();
  std::stringstream os;
  os << stream;
  _base_dir /= os.str();

  _filenames = find_tick_files();
}

TickReplayer::~TickReplayer() {}

Time TickReplayer::get_next_event_time()
{
  // if we don't have a current file reader, or, we dont have one but it has no
  // more events, attempt to get the next file reader
  if (!_reader || !_reader->has_next_event()) {
    get_next_file_reader();
  }

  // if still don't have a file reader, return end-of-event indicator
  if (!_reader)
    return apex::Time{};

  if (_reader->has_next_event())
    return _reader->next_event_time();
  else
    return apex::Time{};
}


void TickReplayer::consume_next_event() {
  _reader->consume_next_event();
}


void TickReplayer::get_next_file_reader()
{
  _reader.reset();
  while (!_reader && !_filenames.empty()) {
    auto next_filename = _filenames.front();
    _filenames.pop_front();

    // create a filereader object
    auto reader = std::make_unique<TickFileReader>(next_filename, _mktdata, _stream);

    LOG_INFO("tickbin first event time: " << reader->next_event_time());

    // scan for an initial event that falls within the current replay period;
    // this typically happens for the first file that is part of a backtest.

    reader->wind_forward(_replay_from);
    if (reader->has_next_event()) {
      _reader = std::move(reader);
    }
  }
}

std::list<std::filesystem::path> TickReplayer::find_tick_files()
{
  namespace fs = std::filesystem;

  std::list<std::filesystem::path> filenames;
  for (auto& date : _dates) {
    auto fn = build_tickbin_filename(date);
    if (fs::exists(fn) && fs::is_regular_file(fn)) {
      filenames.push_back(fn);
    }
  }

  return filenames;
}

std::filesystem::path TickReplayer::build_tickbin_filename(apex::Time time)
{
  auto fn = _base_dir;
  fn /= time.strftime("%Y");
  fn /= time.strftime("%m");
  fn /= time.strftime("%d");
  fn /= _instrument.native_symbol();
  fn += ".bin";
  return fn;
}


} // namespace apex
