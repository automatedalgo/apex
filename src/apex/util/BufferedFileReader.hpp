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

#include <cstring>
#include <vector>

namespace apex {

/* Read a file into local buffer.  File opening & closing is not managed
 * by this class. */
template <typename T>
class BufferedFileReader {

public:
  explicit BufferedFileReader(T& file,
                              std::size_t init_buffer_size = 1024*1024)
    : _file(file),
      _buf(init_buffer_size, '\0'),
      _end(_buf.data() + _buf.size()),
      _head(_buf.data()),
      _tail(_head)
  {
  }

  BufferedFileReader(const BufferedFileReader&) = delete;
  BufferedFileReader& operator=(const BufferedFileReader&) = delete;

  std::size_t read() {
    if (!_file.is_open() ||_file.is_eof())
      return 0;

    this->recycle();

    if (space() == 0)
      return 0;

    // TODO: add detection of file that has failed

    auto buf_ptr = _tail;
    auto buf_len = space();

    // attempt to read as many bytes that fit into our buffer
    auto nread = _file.read(buf_ptr, buf_len);
    if (nread > 0)
      _tail += nread;

    return nread;
  }

  std::size_t recycle() {
    if (_tail == _end) {
      // the tail pointer, where new bytes are destined to be written, has
      // reached the buffer end, so no more bytes can be written until we move
      // pending data to the front of the buffer

      if (_head > _buf.data()) {
        const auto avail = this->avail();
        ::memmove(_buf.data(), _head, avail);
        _head = _buf.data();
        _tail = _head + avail;
        return avail;
      }
    }
    return 0;
  }

  T& file() { return _file; }

  void discard(std::size_t len) {

    // TODO: in debug mode, add length check, len <= avail
    _head += len;
  }

  char* data() { return _head; }

  /* Number of bytes ready to be processed */
  [[nodiscard]] std::size_t avail() const { return _tail - _head; }

  /* Buffer space that can be used for writing data to. */
  [[nodiscard]] std::size_t space() const { return _end - _tail; }

private:
  T& _file;
  std::vector<char> _buf;
  char * _end;  // pointer to first byte out-side buffer
  char * _head; // pointer to data start
  char * _tail; // pointer to 1-past data end
};


}
