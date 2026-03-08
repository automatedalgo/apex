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

#include <apex/util/TimeLog.hpp>
#include <apex/core/TimeLogMemmap.hpp>

#include <memory>

namespace apex
{

class Core;

class TimeLogService
{
public:
  static constexpr int bits = 24;  // 16 million rows
  static constexpr int rows = 1 << bits;
  static constexpr int mask = (1 << bits)-1;

  explicit TimeLogService(Core* core);
  ~TimeLogService();

  inline void store(TimeLog& tl) {
    _records[_idx & mask].tp[0] = tl.at_io.to_int();
    _records[_idx & mask].tp[1] = tl.at_read.to_int();
    _records[_idx & mask].tp[2] = tl.at_ssl.to_int();
    _records[_idx & mask].tp[3] = tl.at_message.to_int();
    _records[_idx & mask].tp[4] = tl.at_parsed.to_int();
    _records[_idx & mask].tp[5] = tl.at_book.to_int();
    _records[_idx & mask].msgid = _idx & 0xFFFFFFFF;  // 32 bit int
    _idx++;
  }

private:
  Core* _core;
  TimingRecord* _records;
  unsigned long _idx;
  std::unique_ptr<TimeLogMemMap> _mmap;

};

} // namespace
