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

#include <apex/core/TimeLogService.hpp>
#include <apex/util/utils.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/Time.hpp>
#include <apex/util/Error.hpp>

// Defined by glibc to provide the program name
extern char *program_invocation_short_name;

namespace apex
{
std::string generate_auto_file_name()
{
  Time t = Time::realtime_now();
  auto base_path = apex_home() / "log";
  base_path /= std::string(program_invocation_short_name);
  base_path += t.strftime(".%Y%m%d-%H%M%S");
  base_path += ".timelog";
  return base_path.string();
}


TimeLogService::TimeLogService(Core* core)
  : _core (core),
    _records(nullptr),
    _idx(0)
{
  auto filename = generate_auto_file_name();
  _mmap = std::make_unique<TimeLogMemMap>(filename,
                                          true,
                                          rows);
  _records = _mmap->records();
}


TimeLogService::~TimeLogService() = default;


} // namespace
