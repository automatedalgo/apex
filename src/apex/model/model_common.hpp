/* Copyright 2026 Automated Algo (www.automatedalgo.com)

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

#include <string_view>

namespace apex
{

/* Model the immediate result status of an asynchronous order request. */
class SendStatus {
public:

  SendStatus()
    : _success(true) {
  }

  explicit SendStatus(std::string_view err_msg)
    : _success(false),
      _err_msg(err_msg)
  {
  }

  explicit operator bool() const {
    return _success;
  }

  std::string_view err_msg() const {
    return _err_msg;
  }

  static const SendStatus success;

private:
  bool _success;
  std::string_view _err_msg;

};

}
