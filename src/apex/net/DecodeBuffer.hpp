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

#include <vector>

#include <cstddef>

namespace apex
{

/* Represent a buffer of bytes that have been read off a socket and are awaiting
 * decode. */
class DecodeBuffer
{
public:
  struct read_pointer {
    read_pointer(char* p , size_t avail) : _ptr(p), _avail(avail) {}

    char operator[](size_t i) const { return _ptr[i]; }

    char& operator[](size_t i) { return _ptr[i]; }

    void advance(size_t i)
    {
      _ptr += i;
      _avail -= i;
      _consumed += i;
    }

    size_t avail() const { return _avail; }

    size_t consumed() const { return _consumed; }

    const char* ptr() const { return _ptr; }

    char* ptr() { return _ptr; }

  private:
    char* _ptr;
    size_t _avail;
    size_t _consumed;
  };

  DecodeBuffer(size_t initial_size, size_t max_size, size_t padding = 64);

  /* size of data present in the buffer ready to be processed */
  size_t avail() const { return _bytes_avail; }

  /* current space for new data */
  size_t space() const { return buffer_size() - _bytes_avail; }

  /* pointer to data */
  char* data() { return _mem.data(); }

  /* copy in new bytes, growing internal space if necessary */
  size_t consume(const char* src, size_t len);

  /* obtain a read pointer */
  read_pointer read_ptr() { return {_mem.data(), _bytes_avail}; }

  void discard(const read_pointer&);

  size_t pad_size() const { return _pad_size; }

  void clear() { _bytes_avail = 0; }

  /* current buffer capacity (this is the max data that can be held) */
  size_t buffer_size() const { return _mem.size() - _pad_size;}

private:
  void grow_by(size_t len);
  void reset_padding();

  size_t _bytes_avail;
  std::vector<char> _mem;
  size_t _max_size;
  size_t _pad_size;
};

} // namespace apex
