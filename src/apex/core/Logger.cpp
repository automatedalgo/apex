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

#include <apex/core/Logger.hpp>
#include <apex/util/platform.hpp>
#include <apex/util/utils.hpp>
#include <apex/util/Config.hpp>

#include <functional>
#include <iostream>

namespace apex
{


static const size_t thread_width = 18;

static std::string format_threadid(int tid_int, const std::string& tname)
{
  std::array<char, thread_width + 1> buf; // +1 for null
  memset(buf.data(), ' ', thread_width);

  std::ostringstream oss;
  oss << "| " << tid_int << "/" << tname;
  auto raw = oss.str();

  // memcpy into the buf
  auto copy_len = std::min(thread_width, raw.length());
  memcpy(buf.data(), raw.data(), copy_len);

  buf[buf.size() - 1] = '\0';
  buf[buf.size() - 2] = ' ';

  return std::string(buf.data(), thread_width);
}


static const char* level_str(Logger::level l)
{
  switch (l) {
    case Logger::level::error:
      return "| ERROR | ";
    case Logger::level::warn:
      return "| WARN  | ";
    case Logger::level::note:
      return "| NOTE  | ";
    case Logger::level::info:
      return "| INFO  | ";
    case Logger::level::debug:
      return "| DEBUG | ";
    default:
      return "| ????  | ";
  }
}

std::vector<std::string> create_banner() {
  auto multistring=R"""(
   __ _ _ __   _____  __
  / _` | '_ \ / _ \ \/ /
 | (_| | |_) |  __/>  <
  \__,_| .__/ \___/_/\_\
       |_|
)""";
  return split(multistring, '\n');
}

/* Constructor */
Logger::Logger() : m_mask(mask_level_and_above(level::info)) {}

/* Destructor */
Logger::~Logger() {}

int Logger::mask_level_and_above(level lvl)
{
  return 0 |
    ((level::debug >= lvl) ? level::debug : 0) |
    ((level::info >= lvl) ? level::info : 0) |
    ((level::note >= lvl) ? level::note : 0) |
    ((level::warn >= lvl) ? level::warn : 0) |
    ((level::error >= lvl) ? level::error : 0);
}


void Logger::log_banner(RunMode mode) {
  std::string mode_name;
  switch (mode) {
    case RunMode::paper:
      mode_name = "paper trading";
      break;
    case RunMode::live:
      mode_name = "LIVE TRADING";
      break;
    case RunMode::backtest:
      mode_name = "backtest";
      break;
    default:
      mode_name = "unknown";
      break;
  }
  if (!_banner_done) {
    auto banner = create_banner();
    for (size_t i = 1; i < banner.size(); i++){
      std::cout << banner[i];
      if (i == 2) {
        std::cout << "   mode: " << mode_name;
      }
      std::cout << "\n";
    }
    _banner_done = true;
  }
}

void Logger::write(Logger::level lvl, std::string msg, const char* file, int l)
{
  auto guard = std::scoped_lock(m_write_mutex);
  auto parts = split(file, '/');    // TODO: keep empty tokens?
  auto filename = *parts.rbegin();
  auto const tid = apex::thread_id();

  std::string timestamp;

  Time t = _clock_fn? _clock_fn() : Time::realtime_now();
  auto tm = t.tm_utc();
  auto usec = t.usec();
  char buf[32] = {0};
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d | %02d:%02d:%02d.%06lu",
           tm.tm_year+1900,
           tm.tm_mon + 1,
           tm.tm_mday,
           tm.tm_hour,
           tm.tm_min,
           tm.tm_sec,
           usec.count());
  std::cout << buf << "";

  // _detailed_logging = false;
  if (_detailed_logging) {
    auto guard2 = std::scoped_lock(m_thread_ids_mutex);
    auto iter = m_thread_ids.find(tid);
    if (iter == std::end(m_thread_ids)) {
      auto label = format_threadid(tid, "????");
      std::cout << " " << label;
      m_thread_ids[tid] = std::move(label);
    } else {
      std::cout << " " << iter->second;
    }
  }

  std::cout << " " << level_str(lvl) << msg;
  if (_detailed_logging)
    std::cout << " (" << filename << ":" << l << ")";
  std::cout << "\n";
}


Logger& Logger::instance()
{
  static Logger __instance;
  return __instance;
}


void Logger::register_thread_id(std::string label)
{
  auto guard = std::scoped_lock(m_thread_ids_mutex);
  auto tid = apex::thread_id();
  m_thread_ids[tid] = format_threadid(tid, label);
}

void Logger::set_clock_source(std::function<Time(void)> fn)
{
  _clock_fn = std::move(fn);
}


Logger::level Logger::string_to_level(const std::string& s)
{
  if (s == "debug")
    return level::debug;
  if (s == "info")
    return level::info;
  if (s == "note")
    return level::note;
  if (s == "warn")
    return level::warn;
  if (s == "error")
    return level::error;

  throw std::runtime_error("log level not recognised");
}


void Logger::configure_from_config(Config config) {
  auto level_str = config.get_string("level", "info");
  auto detailed_logging = config.get_bool("detailed", false);
  auto level = apex::Logger::string_to_level(level_str);
  apex::Logger::instance().set_level(level);
  apex::Logger::instance().set_detail(detailed_logging);
  apex::Logger::instance().set_is_configured(true); // mark as ready
}

} // namespace apex
