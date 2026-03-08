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

#include <cstdint>
#include <ctime>
#include <string>

namespace apex
{

/* Collect a log of time points, marked at various milestones on the tick to
 * trade journey.
 */
class TimeLog
{
public:

  struct TimePoint
  {
    struct timespec ts = {0,0};

    inline void mark() {
      ::clock_gettime(CLOCK_REALTIME, &ts);
    }

    [[nodiscard]] uint64_t to_int() const {
      return ts.tv_sec * 1e9 + ts.tv_nsec;
    }
  };

  TimePoint at_io;         // at first IO notification
  TimePoint at_read;       // at socket read complete
  TimePoint at_ssl;        // at SSL decoded
  TimePoint at_message;    // at raw message ready
  TimePoint at_parsed;     // at message parsed
  TimePoint at_book;       // at book updated

  std::string dump();
};

}
