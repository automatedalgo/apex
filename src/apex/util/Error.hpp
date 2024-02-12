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

#include <sstream>
#include <stdexcept>

#define THROW( args ) do {                              \
    std::ostringstream _s;                              \
    _s << args;                                         \
    throw apex::Error(__FILE__, __LINE__, _s.str());    \
  } while (0)


namespace apex
{

class Error: public std::runtime_error
{
public:

  Error(std::string filename, int linenumber, std::string msg)
    : std::runtime_error(msg),
      _file(filename),
      _ln(linenumber)
  {
  }

  const std::string& file()  const noexcept { return _file; }
  int line() const noexcept { return _ln; }

private:
  std::string _file;
  int _ln;
};



} // namespace apex
