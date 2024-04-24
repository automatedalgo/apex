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
#include <apex/util/GzFile.hpp>
#include <apex/util/BufferedFileReader.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/model/ExchangeId.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/infra/DecodeBuffer.hpp>
#include <apex/backtest/TickReplayer.hpp>

#include <filesystem>

#include <zlib.h>

namespace apex
{

class TardisDecoder;
class TardisCsvParser;

class TardisFileReader : public BaseTickFileReader
{
public:
  enum class DataType {
    book_snapshot_5,
    trades
  };

  explicit TardisFileReader(std::filesystem::path,
                            MarketData*,
                            MdStream,
                            DataType datatype);
  ~TardisFileReader();

  void wind_forward(apex::Time t) override;

  [[nodiscard]] bool has_next_event() const override;

  [[nodiscard]] apex::Time next_event_time() const override;

  void consume_next_event() override;

private:
  void read_header();
  void parse_next_event();

  std::filesystem::path _fn;
  MarketData* _mktdata;
  DataType _datatype;
  std::size_t _event_count = 0;

  GzFile _file;
  BufferedFileReader<GzFile> _reader;
  std::unique_ptr<TardisCsvParser> _parser;
};


} // namespace apex
