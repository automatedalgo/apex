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

#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <string>


#ifndef STRINGIFY
#define STRINGIFY(n) STRINGIFY_HELPER(n)
#define STRINGIFY_HELPER(n) #n
#endif

namespace apex
{

enum class RunMode {
  paper = 1,     // paper trading
  live = 2,      // live trading
  backtest = 3   // backtest
};
std::string to_string(RunMode);
std::ostream& operator<<(std::ostream&, RunMode);
RunMode parse_run_mode(const std::string& s);

/* Represent the mode of a socket or connection */
enum class connect_mode { active, passive };

/*
 * Split a string based on a single delimiter.
 */
std::vector<std::string> split(const std::string_view& str, char delim);

void create_dir(std::filesystem::path dir);

/* Return user home directory, or empty path if not determined */
std::filesystem::path user_home_dir();

/* Trim leading and trailer spaces */
inline std::string trim(const std::string_view& str)
{
  auto it = str.begin();
  while (it != str.end() && std::isspace(*it))
    it++;
  auto rit = str.rbegin();
  while (rit.base() != it && std::isspace(*rit))
    rit++;
  return {it, rit.base()};
}


/* Trim leading and trailer chars */
inline std::string trim(const std::string_view& str,
                        const std::string_view& chars_to_trim)
{
  auto s = str.find_first_not_of(chars_to_trim);
  if (std::string::npos != s) {
    auto e = str.find_last_not_of(chars_to_trim);
    return std::string{str.substr(s, e - s + 1)};
  } else {
    return std::string{};
  }
}


/* Generate UTC iso8601 timestamp, like YYYY-MM-DDThh:mm:ss.sssZ */
std::string utc_timestamp_iso8601();

/* Generate UTC timestamp, like "20170527-002948.796000" */
std::string utc_timestamp_condensed(bool add_fraction = true);

/* Read entire file */
std::string slurp(const char* filename);

// TODO: revert this back to the return of an error code
std::string HMACSHA256_base4(const char* key, int keylen, const char* msg,
                             int msglen);

void write_json_message(const std::string& dir, std::string msgtype,
                        std::string raw_json);

/** Optionally store a value of value of type T.  Methods to assign
the value and compare with it are protected by an internal mutex.
*/
template <typename T> class synchronized_optional
{
public:
  synchronized_optional() : _valid(false), _value{} {}
  explicit synchronized_optional(T value) : _valid(true), _value(std::move(value)) {}

  void set_value(const T& new_value)
  {
    std::lock_guard<std::mutex> guard(_mutex);
    _value = new_value;
    _valid = true;
  }

  void set_value(T&& new_value)
  {
    std::lock_guard<std::mutex> guard(_mutex);
    _value = std::move(new_value);
    _valid = true;
  }

  void release()
  {
    std::lock_guard<std::mutex> guard(_mutex);
    _valid = false;
  }

  bool compare(const T& value) const
  {
    std::lock_guard<std::mutex> guard(_mutex);
    return _valid && _value == value;
  }

  bool is_valid() const {
    std::lock_guard<std::mutex> guard(_mutex);
    return _valid;
  }

private:
  mutable std::mutex _mutex;
  bool _valid;
  T _value;
};


class scope_guard
{
public:
  template <class Callable>
  scope_guard(Callable&& undo_func) : _fn(std::forward<Callable>(undo_func))
  {
  }

  scope_guard(scope_guard&& other) : _fn(std::move(other._fn))
  {
    other._fn = nullptr;
  }

  ~scope_guard()
  {
    if (_fn)
      _fn(); // must not throw
  }

  void release() noexcept { _fn = nullptr; }

  scope_guard(const scope_guard&) = delete;

  void operator=(const scope_guard&) = delete;

private:
  std::function<void()> _fn;
};

/* Represent values like 0.0001 etc, as  pair (1, -4). Does not have to be
   normalised, eg, the _mantissa can be like 10000. */
struct ScaledInt {

  ScaledInt() : ScaledInt(0, 0) {}
  explicit ScaledInt(int64_t i) : ScaledInt(i, 0) {}
  ScaledInt(int64_t mantissa, int scale);

  ScaledInt(const std::string&);

  bool operator==(const ScaledInt& other) const
  {
    return _mantissa == other._mantissa and _scale == other._scale;
  }

  bool operator!=(const ScaledInt& other) const
  {
    return _mantissa != other._mantissa or _scale != other._scale;
  }

  double exponent_pow10() const { return _exponent_pow10; }

  int64_t mantissa() const { return _mantissa; }
  int scale() const { return _scale; }

  // double round(double) const;
  double trunc(double) const;
  double ceil(double) const;

  // get value as double
  double as_double() const { return _mantissa * _exponent_pow10; }

private:
  int64_t _mantissa;
  int _scale;
  double _exponent_pow10;
};

std::string demangle(const char* name);

std::string to_hex(const unsigned char* p, size_t size);

std::string to_hex(const char* p, size_t size);

std::string int32_to_base16(uint32_t i, bool trim_leading_zeros = false);

void log_exception(const char* site);

std::string format_double(double d, bool trim_zeros = false, int precision = 9);

void log_message_exception(const char* source, std::string data);

std::string str_toupper(std::string s);
std::string str_tolower(std::string s);

// Return if double value is non-zero, and also is not nan and non inf.
bool is_finite_non_zero(double d);


template <typename E> constexpr auto to_underlying(E e) noexcept
{
  return static_cast<std::underlying_type_t<E>>(e);
}

/* Install a signal handler for SIGINT (Control-C), and wait
 * for it occur. */
void wait_for_sigint();

} // namespace apex
