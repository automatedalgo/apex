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

#include <filesystem>

namespace apex
{

struct TickbinHeader {
  constexpr static size_t header_lead_length = 16;

  // version of the tickbin format
  std::string version;

  // entire header length, including header fields, meta-data and padding.
  size_t length;
};

TickbinHeader decode_tickbin_file_header(const char* ptr);

struct StreamInfo {
  Instrument instrument;
  std::string channel;

  bool operator<(const StreamInfo& rhs) const {
    if (instrument < rhs.instrument)
      return true;
    else if (instrument == rhs.instrument && channel < rhs.channel)
      return true;
    else
      return false;
  }

  ExchangeId exchange_id() const { return instrument.exchange_id(); }

  const std::string& symbol() const { return instrument.symbol(); }
};

struct TickFileBucketId {
  int year = 0;
  int month = 0;
  int day = 0;

  static TickFileBucketId from_time(apex::Time t)
  {
    auto p = t.tm_utc();
    return {std::max(p.tm_year + 1900, 0), std::max(p.tm_mon + 1, 0),
            std::max(p.tm_mday, 0)};
  }

  [[nodiscard]] std::string as_string() const
  {
    char buf[16] = {0};
    snprintf(buf, sizeof(buf), "%04d%02d%02d", year, month, day);
    return buf;
  };
};


class TickbinDecoder;

class TickFileReader
{
public:
  explicit TickFileReader(std::filesystem::path fn, MarketData*, MdStream stream_type);
  ~TickFileReader();

  void wind_forward(apex::Time t);

  [[nodiscard]] bool has_next_event() const;

  [[nodiscard]] apex::Time next_event_time() const;

  void consume_next_event();

private:
  std::filesystem::path _fn;
  MarketData* _mktdata;
  std::tuple<size_t, json> parse_mmap_header(char* ptr);

  std::unique_ptr<TickbinDecoder> _decoder;
};

} // namespace apex
