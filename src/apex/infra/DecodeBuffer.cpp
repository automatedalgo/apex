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


DecodeBuffer::DecodeBuffer(size_t size, size_t max_size, size_t padding)
  : _bytes_avail(0),
    _mem(size+padding, 0),
    _max_size(max_size),
    _pad_size(padding)
{
  reset_padding();
}


size_t DecodeBuffer::consume(const char* src, size_t len)
{
  if (space() < len)
    grow_by(len - space());

  size_t consume_len = (std::min)(space(), len);
  if (len && consume_len == 0)
    throw std::runtime_error("DecodeBuffer full, cannot consume data");

  memcpy(data() + _bytes_avail, src, consume_len);
  _bytes_avail += consume_len;

  return consume_len;
}


void DecodeBuffer::grow_by(size_t extra_len)
{
  size_t extra_max = _max_size - buffer_size();
  size_t grow_size = std::min(extra_max, extra_len);
  if (grow_size) {
    _mem.resize(_mem.size() + _pad_size + grow_size);
     reset_padding();
  }
}


void DecodeBuffer::discard(const read_pointer& rd)
{
  _bytes_avail = rd.avail();
  if (rd.ptr() != data() && rd.avail())
    memmove(data(), rd.ptr(), rd.avail());
}


void DecodeBuffer::reset_padding()
{
  if (_pad_size)
    memset(data() + buffer_size(), ' ', _pad_size);
}


} // namespace apex
