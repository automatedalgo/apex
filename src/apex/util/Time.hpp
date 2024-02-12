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

#include <apex/util/platform.hpp>

#include <chrono>

namespace apex
{

class Time
{

public:
  enum class Resolution { milli, micro };

  Time() : _tv{0, 0} {}
  Time(int sec, std::chrono::milliseconds ms) : _tv{sec, ms.count() * 1000} {}
  Time(int sec, std::chrono::microseconds us) : _tv{sec, us.count()} {}
  Time(const Time& other) = default;
  explicit Time(std::chrono::microseconds);
  explicit Time(const char* s);
  explicit Time(const std::string& s);
  explicit Time(TimeVal tv) : _tv{tv} {}

  /* Caution -- this must not be used by any part of the program/strategy that
   * might operate in backtest mode. This always returns the real world
   * wall-clock time, never the simulation time. */
  static Time realtime_now();

  Time& operator=(const Time& other)
  {
    this->_tv = other._tv;
    return *this;
  }

  void operator+=(std::chrono::milliseconds interval)
  {
    auto usec = _tv.usec + interval.count() * 1000;
    auto secs = usec / 1000000;
    auto remainder = usec % 1000000;
    _tv.sec += secs;
    _tv.usec = remainder;
  }


  bool operator==(const Time& other) const
  {
    return this->_tv.sec == other._tv.sec && this->_tv.usec == other._tv.usec;
  }


  bool operator!=(const Time& other) const {
    return !(*this == other);
  }

  [[nodiscard]] std::chrono::milliseconds as_epoch_ms() const
  {
    return std::chrono::milliseconds(_tv.sec * 1000 + _tv.usec / 1000);
  }

  [[nodiscard]] std::chrono::microseconds as_epoch_us() const
  {
    return std::chrono::microseconds(_tv.sec * 1000000 + _tv.usec);
  }

  bool operator<=(const Time& other) const
  {
    return *this == other || *this < other;
  }

  bool operator>=(const Time& other) const
  {
    return *this == other || *this > other;
  }

  bool operator>(const Time& other) const
  {
    return (this->_tv.sec > other._tv.sec) or
           ((this->_tv.sec == other._tv.sec) and
            (this->_tv.usec > other._tv.usec));
  }

  bool operator<(const Time& other) const
  {
    return (this->_tv.sec < other._tv.sec) or
           ((this->_tv.sec == other._tv.sec) and
            (this->_tv.usec < other._tv.usec));
  }

  [[nodiscard]] bool empty() const { return _tv.sec == 0 && _tv.usec == 0; }

  // The `simpler` option makes the datetime string a little more human readable
  // format
  [[nodiscard]] std::string as_iso8601(Resolution = Resolution::milli, bool simpler=false) const;

  /* Return the UTC representation in a struct tm */
  [[nodiscard]] struct tm tm_utc() const;


  [[nodiscard]] std::chrono::microseconds usec() const {
    return std::chrono::microseconds{_tv.usec};
  }

  [[nodiscard]] apex::Time round_to_earliest_day() const;

  [[nodiscard]] std::string strftime(const char* format) const;

private:
  struct TimeVal _tv; // implementation
};


std::chrono::microseconds operator-(const Time&, const Time&);

std::ostream& operator<<(std::ostream&, const Time&);



} // namespace apex
