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
#include <apex/backtest/TardisFileReader.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/core/MarketDataService.hpp>
#include <apex/util/Error.hpp>

#include <functional>

namespace apex
{

TickReplayer::TickReplayer(std::string base_directory,
                           std::string tick_format,
                           const Instrument& instrument,
                           MarketData* mktdata,
                           MdStream stream, Time replay_from,
                           Time replay_upto, std::list<Time> dates)
  : _tick_format(tick_format),
    _instrument(instrument),
    _mktdata(mktdata),
    _stream(stream),
    _replay_from(replay_from),
    _replay_upto(replay_upto),
    _dates(dates)
{
  _base_dir = base_directory;
  _base_dir /= instrument.exchange_name();
  build_tick_file_options();
  _filenames = find_tick_files();
}

TickReplayer::~TickReplayer() {}

Time TickReplayer::get_next_event_time()
{
  // if we don't have a current file reader, or, we have one but it has no
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

    // create a tick-file reader for the appropriate tick format
    std::unique_ptr<BaseTickFileReader> reader;

    if (_tick_format == "tardis") {
      reader = _tick_reader_factory(next_filename);

      LOG_INFO("tick-file first event time: " << reader->next_event_time());

      // Scan for an initial event that falls within the current replay period;
      // this typically happens for the first file that is part of a
      // backtest. Allowing the tick-file reader to perform this action allows it
      // to use any internal short-cuts to decide which events to skip.
      reader->wind_forward(_replay_from);
    }

    // if the current reader has an event that we can use, then we retain the
    // reader
    if (reader->has_next_event())
      _reader = std::move(reader);
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
      //LOG_INFO("found: " << fn);
    } else {
      //LOG_INFO("not found: " << fn);
    }
  }

  return filenames;
}


void TickReplayer::build_tick_file_options()
{
  if (_tick_format == "tardis") {
    std::string subdir;
    TardisFileReader::DataType datatype;

    switch (_stream) {
      // tardis-csv datasets doesn't have aggtrades, so, for aggtrade request we
      // can just use trades
      case MdStream::AggTrades :
      case MdStream::Trades :
        subdir = "trades";
        datatype = TardisFileReader::DataType::trades;
        break;

      // tardis-csv doesn't have an L1 datasets, so, we just refer to smallest
      // depth book snapshot
      case MdStream::L1 :
        subdir = "book_snapshot_5";
        datatype = TardisFileReader::DataType::book_snapshot_5;
        break;

      default: {
        THROW("Tardis tick-replayer doesn't support stream type " << _stream);
      }
    };

    _tick_subdir = subdir;
    _tick_reader_factory = [this, datatype](std::filesystem::path fn)
    {
      return std::make_unique<TardisFileReader>(
        fn,
        this->_mktdata,
        this->_stream,
        datatype);
    };

  }
  else {
    std::ostringstream  oss;
    oss << "don't know how to locate tick-files for format '" << _tick_format << "' and stream '" << _stream << "'";
    throw std::runtime_error(oss.str());
  }
}

std::filesystem::path TickReplayer::build_tickbin_filename(apex::Time time)
{
  auto fn = _base_dir;
  fn /= _tick_subdir;
  fn /= time.strftime("%Y");
  fn /= time.strftime("%m");
  fn /= time.strftime("%d");
  fn /= _instrument.native_symbol();
  fn += ".csv.gz";
  return fn;
}


} // namespace apex
