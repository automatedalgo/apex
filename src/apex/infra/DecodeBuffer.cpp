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

#include <apex/infra/DecodeBuffer.hpp>

#include <cstring>
#include <stdexcept>

namespace apex
{


DecodeBuffer::read_pointer::read_pointer(char* p, size_t avail)
  : _ptr(p), _avail(avail)
{
}


DecodeBuffer::DecodeBuffer(size_t initial_size, size_t max_size)
  : _mem(initial_size), _max_size(max_size), _bytes_avail(0)
{
}


void DecodeBuffer::update_max_size(size_t new_max)
{
  if (new_max == _max_size)
    return;

  if ((new_max < _max_size) && (new_max < _mem.size()))
    throw std::runtime_error("unable to reduce DecodeBuffer max size");

  _max_size = new_max;

  // Note: don't perform any actual DecodeBuffer modification, since it would
  // invalidate any read_pointer
}


size_t DecodeBuffer::consume(const char* src, size_t len)
{
  if (space() < len)
    grow_by(len - space());

  size_t consume_len = (std::min)(space(), len);
  if (len && consume_len == 0)
    throw std::runtime_error("DecodeBuffer full, cannot consume data");

  memcpy(_mem.data() + _bytes_avail, src, consume_len);
  _bytes_avail += consume_len;

  return consume_len;
}

void DecodeBuffer::grow_by(size_t len)
{
  size_t grow_max = _max_size - _mem.size();
  size_t grow_size = (std::min)(grow_max, len);
  if (grow_size)
    _mem.resize(_mem.size() + grow_size);
}


void DecodeBuffer::discard(const read_pointer& rd)
{
  _bytes_avail = rd.avail();
  if (rd.ptr() != _mem.data() && rd.avail())
    memmove(_mem.data(), rd.ptr(), rd.avail());
}

} // namespace apex
