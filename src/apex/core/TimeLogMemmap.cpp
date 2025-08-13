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

#include <apex/core/TimeLogMemmap.hpp>
#include <apex/util/Error.hpp>
#include <apex/core/Logger.hpp>

#include <string>

namespace apex
{


static size_t round_up_to_page(size_t size, size_t page_size) {
    return (size % page_size == 0) ? size : size + (page_size - (size % page_size));
}


TimeLogMemMap::TimeLogMemMap(std::filesystem::path filename,
                           bool write_mode,
                           unsigned record_count)
  : _write_mode(write_mode),
    _map(nullptr),
    _filesize(0),
    _data(nullptr)
{
  init_memmap_file(filename, write_mode, record_count);

  // set the preamble
  if (write_mode) {
    strncpy(_data->header.preamble, "MMAP_TIME_1", sizeof(((TimingHeader*)0)->preamble));
    _data->header.row_capacity =  record_count;
  }
  else {
    if (strcmp(_data->header.preamble, "MMAP_TIME_1") != 0 ) {
      THROW("memmap file corrupted, preamble bad, " << filename);
    }
  }
}


TimeLogMemMap::~TimeLogMemMap()
{
  if (_map) {
    if (_write_mode)
      ::msync(_map, _filesize, MS_SYNC); // write
    ::munmap(_map, _filesize); // unmap
  }
}


size_t TimeLogMemMap::calc_filesize(unsigned n_rec)
{
  auto data_size = sizeof(TimingMMap) +  n_rec * sizeof(TimingRecord);
  return round_up_to_page(data_size, ::getpagesize());
}


void TimeLogMemMap::init_memmap_file(std::filesystem::path filename,
                                     bool write_mode,
                                     unsigned record_count)
{
  std::string fn = filename.native();

  // if a memmap file exists, delete, we will recreate (just in case we are
  // changing the size)
  auto file_exsits = ::access(fn.c_str(), F_OK) == 0;
  if (file_exsits && write_mode) {
    if (::unlink(fn.c_str()) != 0) {
      THROW("unlink failed, file " << fn << ", errno " << errno);
    }
  }
  if (!file_exsits && !write_mode) {
    THROW("file open failed, file " << fn << ", errno " << errno);
  }

  // open file
  LOG_INFO("opening memmap file " << filename);
  int permissions = 0666; // rw-rw-rw-, umask will remove bits
  int filemode = write_mode? O_CREAT|O_RDWR : O_RDONLY;
  int fd = ::open(fn.c_str(), filemode, permissions);
  if (fd == -1) {
    THROW("open failed, file " << fn << ", errno " << errno);
  }

  // truncate the new memmap to desired size
  if (write_mode) {
    _filesize = calc_filesize(record_count);
    if (::ftruncate(fd, _filesize) == -1) {
      close(fd);
      THROW("truncte failed, file " << fn << ", errno " << errno);
    }
  }

  // for read-only mode, observe the file size
  if (!write_mode) {
    struct stat stat_buf = {};
    if (fstat(fd, &stat_buf) < 0) {
      THROW("fstat failed, file " << fn << ", errno " << errno);
    }
    _filesize = stat_buf.st_size;
  }

  // map file into memory
  int mapmode = write_mode?  PROT_READ | PROT_WRITE : PROT_READ;
  auto map = ::mmap(NULL, _filesize, mapmode, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    THROW("mmap failed, file " << fn << ", errno " << errno);
  }
  ::close(fd);  // can now close the file
  _map = map;
  _data = (TimingMMap *)_map;
}


} // namespace
