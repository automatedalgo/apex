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

#include <filesystem>

#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cerrno>

namespace apex
{

constexpr int timing_rec_size = 128;

struct TimingHeader
{
  char preamble[16];
  unsigned row_capacity;
  char unused[timing_rec_size-16-4];
};
static_assert(sizeof(TimingHeader)==timing_rec_size);

struct TimingRecord
{
  static constexpr int tp_capacity = 10;
  char pathid[24];    // 24 identify path type
  uint64_t tp[tp_capacity];    // 80
  char labels[tp_capacity*2];  // 20
  uint32_t msgid;     //  4
};
static_assert(sizeof(TimingRecord)==timing_rec_size);

struct TimingMMap
{
  TimingHeader header;
  TimingRecord records[];
};


class TimeLogMemMap
{
public:
  TimeLogMemMap(std::filesystem::path filename,
               bool write_mode,
               unsigned record_count = 0);
  ~TimeLogMemMap();

  TimingRecord* records() { return _data->records; }
  TimingMMap* data() { return _data; }

private:
  size_t calc_filesize(unsigned record_count);
  void init_memmap_file(std::filesystem::path, bool, unsigned);

  bool _write_mode;
  void * _map;
  size_t _filesize;
  TimingMMap * _data;
};


} // namespace
