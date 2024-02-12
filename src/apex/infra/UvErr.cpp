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

#include <apex/infra/UvErr.hpp>

#include <uv.h>

namespace apex
{

const char* UvErr::message() const
{
  if (_value == 0)
    return "";
  else {
    const char* s = uv_strerror(_value); /* Can leak memory */
    return s ? s : "unknown";
  }
}

bool UvErr::is_eof() const {
  return _value == UV_EOF;
}

} // namespace apex
