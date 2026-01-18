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


#pragma once

#include <cstdint>

namespace apex
{

/* */
class RingBuffer {
public:
  explicit RingBuffer(uint64_t capacity);
  ~RingBuffer();

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  uint64_t capacity() const { return _capacity; }

  uint64_t used() const { return _wi - _ri; }
  uint64_t space() const { return _capacity - used(); }

  bool full() const { return used() == _capacity; }
  bool empty() const { return _wi == _ri; }

  char* read_ptr() const { return _region + _ri % _capacity; }
  char* write_ptr() const { return _region + _wi % _capacity; }

  void advance_read_ptr(uint64_t n) { _ri += n ; }
  void advance_write_ptr(uint64_t n) { _wi += n ; }

private:
  char * _region;
  uint64_t _capacity;
  uint64_t _ri; // read index
  uint64_t _wi; // write index
};

}
