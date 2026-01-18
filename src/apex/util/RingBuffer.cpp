/* Copyright 2025 Automated Algo (www.automatedalgo.com)

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

#include <apex/util/RingBuffer.hpp>
#include <apex/util/utils.hpp>
#include <apex/core/Logger.hpp>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>

namespace apex
{

inline void throw_errno(const char* msg)
{
    throw std::system_error(errno, std::generic_category(), msg);
}

RingBuffer::~RingBuffer() {
  if (_region)
    munmap(_region, _capacity*2);
}

RingBuffer::RingBuffer(uint64_t capacity)
  : _region(nullptr),
    _capacity(capacity?capacity:1),
    _ri(0),
    _wi(0)

{
  int fd = -1;
  apex::scope_guard auto_close_fd = [&fd](){
  if (fd != -1)
    close(fd);
  };

  // 3 call to mmap required: 1 to reserve memory space, and then 2 to map
  // memory space to our physical memory

  size_t pagesize = sysconf(_SC_PAGESIZE);
  _capacity = (_capacity + pagesize - 1) & ~(pagesize - 1);  // round up


  // Create a file in memory, this will be our physical memory
  fd = memfd_create("ringbuffer", 0);
  if (fd == -1)
    throw_errno("memfd_create failed");

  // adjust size to required size
  if (ftruncate(fd, _capacity) == -1) {
    throw_errno("ftruncate failed"); // will take errno from close!
  }


  // reserve a virtual memory address region where we can later map our physcial
  // memory twice contiguously.  PROT_NONE is used to mark this region are not
  // accessible.
  _region = (char*) mmap(NULL,
                         _capacity * 2,
                         PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
  if (_region == MAP_FAILED) {
  }

  // map the first half to physical page
  void* first = mmap(
    _region,
    _capacity,
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_FIXED,
    fd,
    0
    );
  if (first == MAP_FAILED) {
    throw_errno("mmap failed");
  }

  // map the second half to physical page
  void* second = mmap(
    (char*) _region + _capacity,
    _capacity,
    PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_FIXED,
    fd,
    0
    );
  if (second == MAP_FAILED) {
    throw_errno("mmap failed");
  }

  memset(_region, 0, _capacity);
}

}
