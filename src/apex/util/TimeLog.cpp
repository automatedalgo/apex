
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

#include <apex/util/TimeLog.hpp>
#include <apex/util/utils.hpp>

#include <time.h>
#include <sstream>

namespace apex
{

static inline long nsec_diff(TimeLog::TimePoint a, TimeLog::TimePoint b) // a-b
{
  return (a.ts.tv_sec - b.ts.tv_sec)*1000000000 + (a.ts.tv_nsec - b.ts.tv_nsec);
}


static std::string timespec_to_string(const struct timespec ts)
{
  char buffer[32];

  // Format: "<seconds>.<nanoseconds padded to 9 digits> s"
  snprintf(buffer, sizeof(buffer)-1, "%ld.%09ld", ts.tv_sec, ts.tv_nsec);
  return buffer;
}


class LatencyReport
{
public:
  LatencyReport(TimeLog::TimePoint t0)
    : _t0(t0),
      _tprev(t0) {
    _oss << timespec_to_string(t0.ts);
  }

  void next(TimeLog::TimePoint t) {
    if (t.ts.tv_sec) {
      _oss << "," << format_double(nsec_diff(t, _tprev)/1000.0,true,1); // t - _tprev
      _tprev = t;
    }
    else {
      _oss << ",";
    }
  }

  std::string str() {
    return _oss.str();
  }

private:
  TimeLog::TimePoint _t0;
  TimeLog::TimePoint _tprev;
  std::ostringstream _oss;
};


std::string TimeLog::dump()
{
  LatencyReport report{at_io};
  report.next(at_read);
  report.next(at_ssl);
  report.next(at_message);
  report.next(at_parsed);
  report.next(at_book);
  return report.str();
}

} // namepace
