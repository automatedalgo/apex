/* Copyright 2024 Automated Algo (www.automatedalgo.com)

This file is part of Automated Algo's "Apex" project.

Apex is free softwarg: you can redistribute it and/or modify it under the terms
of the GNU Lesser General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Apex is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with Apex. If not, see <https://www.gnu.org/licenses/>.
*/


#include <apex/backtest/TardisFileReader.hpp>
#include <apex/backtest/TardisCsvParsers.hpp>
#include <apex/util/BufferedFileReader.hpp>
#include <apex/backtest/TickFileReader.hpp>
#include <apex/backtest/TickbinMsgs.hpp>
#include <apex/core/Logger.hpp>
#include <apex/model/tick_msgs.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/util/Error.hpp>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace apex
{

TardisFileReader::TardisFileReader(std::filesystem::path fn,
                                   MarketData* mktdata,
                                   MdStream stream_type,
                                   DataType datatype)
  :_fn(fn),
   _mktdata(mktdata),
   _datatype(datatype),
   _reader(_file)
{
  namespace fs = std::filesystem;
  if (!fs::exists(fn) || !fs::is_regular_file(fn)) {
    THROW("Tardis tick-data file not found " << fn);
  }
  LOG_INFO("reading Tardis tick-file " << fn);

  _file.open(fn);
  if (!_file.is_open()) {
    THROW("failed to open Tardis tick-data file" << fn);
  }

  this->read_header();
}


TardisFileReader::~TardisFileReader()
{
  LOG_INFO("event count: " << _event_count);
}


void TardisFileReader::read_header()
{
  auto nread = _reader.read(); // initial read of bytes from the file

  switch (_datatype) {
    case  DataType::book_snapshot_5:
      _parser = std::make_unique<TardisCsvParserBookSnapshot5>(_reader.data(), _reader.avail());
      break;
    case  DataType::trades:
      _parser = std::make_unique<TardisCsvParserTrades>(_reader.data(), _reader.avail());
      break;
    default:
      THROW("Tardis parser doesn't support datatype");
  };

// read the header line, check that it has the fields and order we expect

  if (_parser->next())
    _parser->check_header();

  // parse the first record, because we no longer require the header
  // line to be sitting in the parser
  this->parse_next_event();
}

void TardisFileReader::wind_forward(apex::Time t)
{
  size_t skipped = 0;
  apex::Time earliest_skipped;
  apex::Time latest_skipped;

  while (this->has_next_event() && this->next_event_time() < t) {
    auto next_event_time = this->next_event_time();
    if (skipped == 0)
      earliest_skipped = next_event_time;
    else
      latest_skipped = next_event_time;
    this->parse_next_event();
    skipped++;
  }

  if (skipped == 0) {
    LOG_DEBUG("wind-forward events skipped: "
              << skipped << "; next event time: "
              << this->next_event_time() << ", seeking time: " << t);
  } else {
    LOG_INFO("wind-forward events skipped: "
             << skipped << "; from " << earliest_skipped << " upto "
             << latest_skipped << "; next event time: "
             << this->next_event_time() << ", seeking time: " << t);
  }
}


[[nodiscard]] bool TardisFileReader::has_next_event() const {
  return _parser->parse_ok();
}


[[nodiscard]] apex::Time TardisFileReader::next_event_time() const
{
  return _parser->event_time();
}


void TardisFileReader::parse_next_event()
{
  // TODO: add better error detection in here

  auto parsed = _parser->next();

  // if we failed to parse, try to read in more data
  if (!parsed) {
    _reader.discard(_parser->bytes_parsed());
    auto nread = _reader.read(); // TODO: check failure
    _parser->reset_pointers(_reader.data(), _reader.avail());
    _parser->next();
  }

  _event_count++;
}


void TardisFileReader::consume_next_event()
{
  assert(_parser->parse_ok()); // check we're only called if record parsed

  _parser->apply_event(_mktdata);

  this->parse_next_event();
}


} // namespace apex
