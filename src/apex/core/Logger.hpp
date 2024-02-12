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

#include <apex/util/Time.hpp>
#include <apex/util/utils.hpp>

#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>


namespace apex
{
class Config;

class Logger
{
public:
  enum level {
    debug = 1,
    info = 1 << 2,
    note = 1 << 3,
    warn = 1 << 4,
    error = 1 << 5
  };

  static level string_to_level(const std::string&);

  static int mask_level_and_above(level);

  static int mask_levels_all() { return 0xFF; }

  static int mask_levels_none() { return 0; }

  ~Logger();

  bool wants_level(Logger::level l) const { return l & m_mask; }

  bool is_debug_enabled() const { return level::debug & m_mask; }
  bool is_info_enabled() const { return level::info & m_mask; }
  bool is_note_enabled() const { return level::note & m_mask; }
  bool is_warn_enabled() const { return level::warn & m_mask; }
  bool is_error_enabled() const { return level::error & m_mask; }

  /* */
  void set_level(level l) { set_mask(mask_level_and_above(l)); }

  void set_mask(int mask) { m_mask = mask; }


  void set_detail(bool want_detail) { _detailed_logging = want_detail; }

  void set_is_configured(bool b=true) { _is_configured = b; }
  bool is_configured() const { return _is_configured; }

  int get_mask() const { return m_mask; }

  void set_clock_source(std::function<Time(void)>);

  static Logger& instance();

  static void configure_from_config(Config);

  void write(Logger::level, std::string, const char* file, int l);

  void register_thread_id(std::string);

  void log_banner(RunMode);

private:
  Logger();

  Logger(const Logger&) = delete;

  Logger& operator=(const Logger&) = delete;

  int m_mask;
  std::mutex m_write_mutex;

  std::mutex m_thread_ids_mutex;
  struct ThreadInfo {
    std::string label;
    std::string prefix;
  };
  std::map<int, std::string> m_thread_ids;
  std::function<Time(void)> _clock_fn;
  bool _detailed_logging = false;
  bool _is_configured = true;
  bool _banner_done = false;
};


#define _APEX_LOGIMPL_(msg, LEVEL)                                      \
  do {                                                                  \
    apex::Logger& logger = apex::Logger::instance();                    \
    if (logger.wants_level(LEVEL)) {                                    \
      std::ostringstream _s;                                            \
      _s << msg;                                                        \
      logger.write(LEVEL, _s.str(), __FILE__, __LINE__);                \
    }} while (0)



#define LOG_DEBUG(X) _APEX_LOGIMPL_(X, apex::Logger::level::debug)

#define LOG_TRACE(X) _APEX_LOGIMPL_(X, apex::Logger::level::trace)

#define LOG_INFO(X) _APEX_LOGIMPL_(X, apex::Logger::level::info)

#define LOG_NOTICE(X) _APEX_LOGIMPL_(X, apex::Logger::level::note)

#define LOG_WARN(X) _APEX_LOGIMPL_(X, apex::Logger::level::warn)

#define LOG_ERROR(X) _APEX_LOGIMPL_(X, apex::Logger::level::error)

#define LOG_LEVEL_ENABLED(LEVEL) (Logger::instance().wants_level(LEVEL))

#ifndef QUOTE
#define QUOTE(X) "'" << X << "'"
#endif

} // namespace apex
