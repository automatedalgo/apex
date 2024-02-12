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

#include <apex/util/Time.hpp>

#include <sstream>
#include <stdexcept>
#include <iomanip>

namespace apex
{


static TimeVal parse_time(const std::string& s)
{
  static const char* const yyyyMmmMddSPhhCmmCss = "%Y-%m-%d %H:%M:%S"; //
  static const char* const yyyyMmmMddSPhhCmm    = "%Y-%m-%d %H:%M";       // len 16
  static const char* const yyyyMmmMddThhCmmCss  = "%Y-%m-%dT%H:%M:%S";
  static const char* const yyyymmddMhhmmss      = "%Y%m%d-%H%M%S"; // len 15
  static const char* const yyyymmddMhhmm        = "%Y%m%d-%H%M"; // "19700101-1000" len:13
  static const char* const yyyymmdd             = "%Y%m%d"; // len 8

  const char* format = nullptr;
  const char* msec = nullptr;
  switch (s.size()) {
    case 8: {
      format = yyyymmdd;
      break;
    }
    case 23: {
      if (s[10] == 'T')
        format = yyyyMmmMddThhCmmCss;
      else
        format = yyyyMmmMddSPhhCmmCss;
      msec = &s[20];
      break;
    }
    case 19: {
      if (s[10] == 'T')
        format = yyyyMmmMddThhCmmCss;
      else
        format = yyyyMmmMddSPhhCmmCss;
      break;
    }
    case 16: {
      format = yyyyMmmMddSPhhCmm;
      break;
    }
    case 15: {
      format = yyyymmddMhhmmss;
      break;
    }
    case 13: {
      format = yyyymmddMhhmm;
      break;
    }
    case 10: {
      format = "%Y-%m-%d";
      break;
    }
    default: {
      std::ostringstream oss;
      oss << "invalid datetime format for '" << s << "'";
      throw std::runtime_error(oss.str());
    }
  };

  struct tm _tm = {};
  strptime(s.c_str(), format, &_tm);
  time_t t = timegm(&_tm);

  if (msec) {
    auto ms = std::stoi(msec);
    return {t, ms * 1000};
  } else {
    return {t, 0};
  }
}

Time::Time(std::chrono::microseconds usec) : _tv{0, 0}
{
  _tv.sec = usec.count() / 1000000;
  _tv.usec = usec.count() - _tv.sec * 1000000;
}


Time::Time(const char* s) : _tv(parse_time(std::string(s))) {}


Time::Time(const std::string& s) : _tv(parse_time(s)) {}

struct tm Time::tm_utc() const
{
  struct tm parts;
  time_t rawtime = this->_tv.sec;

#ifndef _WIN32
  gmtime_r(&rawtime, &parts);
#else
  gmtime_s(&parts, &rawtime);
#endif

  return parts;
}


std::string Time::as_iso8601(Resolution resolution, bool simpler) const
{
  static constexpr char micro_format[] = "2017-05-21T07:51:17.000000Z"; // 27
  static constexpr char milli_format[] = "2017-05-21T07:51:17.000Z";    // 24
  static constexpr char short_format[] = "2017-05-21T07:51:17";         // 19
  static constexpr char date_format[]  = "yyyy-mm-dd";                  // 10
  static constexpr int short_len = 19;

  static_assert(short_len == (sizeof short_format - 1),
                "short_len check failed");

  char buf[32] = {0};
  static_assert(sizeof buf > (sizeof milli_format));
  static_assert(sizeof milli_format > sizeof short_format);
  static_assert(sizeof micro_format > sizeof short_format);
  static_assert((sizeof buf + 1) > sizeof micro_format);

  struct tm parts;
  time_t rawtime = this->_tv.sec;

#ifndef _WIN32
  gmtime_r(&rawtime, &parts);
#else
  gmtime_s(&parts, &rawtime);
#endif

  if (0 == ::strftime(buf, sizeof buf - 1, "%FT%T", &parts))
    return ""; // strftime not successful

  if (simpler)
    buf[sizeof date_format - 1] = ' ';

  // append milliseconds / microseconds
  int ec = -1;
  switch (resolution) {
    case Resolution::milli:
 #ifndef _WIN32
      ec = snprintf(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                    (int)_tv.usec / 1000);
#else
      ec = sprintf_s(&buf[short_len], sizeof(buf) - short_len, ".%03dZ",
                     (int)tv.usec / 1000);
#endif
      break;
    case Resolution::micro:
 #ifndef _WIN32
      ec = snprintf(&buf[short_len], sizeof(buf) - short_len, ".%06dZ",
                    (int)_tv.usec);
#else
      ec = snprintf_s(&buf[short_len], sizeof(buf) - short_len, ".%06dZ",
                      (int)_tv.usec);
#endif
      break;
  }

  if (ec < 0)
    return "";

  switch (resolution) {
    case Resolution::milli:
      buf[sizeof milli_format - 1 - int(simpler)] = '\0';
      break;
    case Resolution::micro:
      buf[sizeof micro_format - 1 - int(simpler)] = '\0';
      break;
  }

  buf[sizeof buf] = '\0';
  return buf;
}


std::ostream& operator<<(std::ostream& os, const Time& t)
{
  os << t.as_iso8601();
  return os;
}


Time Time::realtime_now() { return Time(time_now()); }


std::chrono::microseconds operator-(const Time& a, const Time& b)
{
  return a.as_epoch_us() - b.as_epoch_us();
}


apex::Time Time::round_to_earliest_day() const {
  using namespace std::chrono;
  using namespace std;

  auto hours = std::chrono::duration_cast<std::chrono::hours>(this->as_epoch_us());
  auto days = hours/24;

  return apex::Time(days.count()*24*60*60, std::chrono::microseconds{0});
}


std::string Time::strftime(const char* str) const
{
  auto parts = tm_utc();
  std::ostringstream os;
  os << std::put_time(&parts, str);
  return os.str();
}


} // namespace apex
