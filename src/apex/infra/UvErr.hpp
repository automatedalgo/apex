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

#include <string>

namespace apex
{

/** Stores a libuv system error code, as returned from underlying libuv system
 * call wrappers. */
class UvErr
{
private:
  int _value;

public:
  /** Default constructor represents success (ie not-an-error). */
  UvErr() noexcept : _value(0) {}

  UvErr(int libuv_error_code) noexcept : _value(libuv_error_code) {}

  /** Return libuv error code, eg, UV_EOF indicate end of file */
  int value() const noexcept { return _value; }

  /** Attempt to convert the libuv error value into a OS specific value. Only
   * suitable for unix platforms. */
  int os_value() const noexcept
  {
#ifdef _WIN32
    return m_value;
#else
    return -_value;
#endif
  }

  bool is_eof() const;

  /** Assign a new error value */
  UvErr& operator=(int v) noexcept
  {
    _value = v;
    return *this;
  }

  /** Check if error value is non-zero, indicating an error */
  explicit operator bool() const noexcept { return _value != 0; }

  /* Obtain explanatory error message related to error value */
  const char* message() const;
};

inline bool operator==(UvErr lhs, UvErr rhs) noexcept
{
  return lhs.value() == rhs.value();
}

inline bool operator!=(UvErr lhs, UvErr rhs) noexcept
{
  return lhs.value() != rhs.value();
}

template <typename _CharT, typename _Traits>
std::basic_ostream<_CharT, _Traits>& operator<<(
    std::basic_ostream<_CharT, _Traits>& os, UvErr ec)
{
  return (os << ec.os_value() << "(" << ec.message() << ")");
}

} // namespace apex
