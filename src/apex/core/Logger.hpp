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
#include <apex/core/RunMode.hpp>

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <queue>
#include <fstream>


namespace apex
{

class Config;
class RealtimeEventLoop;
struct LogOpts;


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

  void set_opts(const LogOpts&);

  bool wants_level(Logger::level l) const { return l & _mask; }

  bool is_debug_enabled() const { return level::debug & _mask; }
  bool is_info_enabled() const { return level::info & _mask; }
  bool is_note_enabled() const { return level::note & _mask; }
  bool is_warn_enabled() const { return level::warn & _mask; }
  bool is_error_enabled() const { return level::error & _mask; }

  void set_level(level l) { set_mask(mask_level_and_above(l)); }

  void set_mask(int mask) { _mask = mask; }

  void set_detail(bool want_detail) { _detailed_logging = want_detail; }

  void set_is_configured(bool b=true) { _is_configured = b; }
  bool is_configured() const { return _is_configured; }

  int get_mask() const { return _mask; }

  void set_clock_source(std::function<Time(void)>);

  static Logger& instance();

  // Configure the global instance directly
  static void configure(level level, bool want_detail = false);

  // Configure the global instance from a config object
  static void configure_from_config(Config);

  void write(Logger::level, std::string_view, const char* file, int l);

  void register_thread_id(std::string);

  void log_banner(RunMode, std::ostream* = nullptr);

  bool is_async_mode();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

private:
  Logger();

  void enable_async_mode();
  void drain_async_queue();
  void async_flush();
  void write_to_stream(std::ostream& os,
                       Time log_ts,
                       long thread_id,
                       Logger::level lvl,
                       std::string_view msg,
                       const char* file,
                       int l);

  int _mask;
  std::mutex _write_mutex;

  std::mutex _thread_ids_mutex;
  struct ThreadInfo {
    std::string label;
    std::string prefix;
  };
  std::map<int, std::string> _thread_ids;
  std::function<Time(void)> _clock_fn;
  bool _detailed_logging = false;
  bool _is_configured = true;
  bool _banner_done = false;

  // async logging
  struct LogItem {
    Time ts;
    long thread_id;
    Logger::level level;
    const char* abs_filename;
    int lineno;
    std::string message;
  };
  std::mutex _async_mtx;
  std::unique_ptr<RealtimeEventLoop> _async_thread; // TODO: replace with jthread
  std::queue<LogItem> _async_queue;
  std::ofstream _async_logfile;
  bool _async_logfile_opened = false;
};


/* Logger options */
struct LogOpts {
  // file-name for log file, leave empty to disable log file, or put "auto" to
  // auto generate a file-name
  std::string filename;

  // for an auto generated file-name, what resolution to use for time part
  enum Time { none, day, second} time = LogOpts::second;

  // file creation mode for log files
  enum Mode { trunc, append } mode = LogOpts::trunc;

  // initial logging level
  Logger::level level = Logger::level::info;

  // include source line & thread details
  bool detail = false;

  // enable asynchronous logging
  bool async = false;

  // support auto log file
  bool auto_filename = true;
};


#define _APEX_LOGIMPL_(msg, LEVEL)                              \
  do {                                                          \
    apex::Logger& logger = apex::Logger::instance();            \
    if (logger.wants_level(LEVEL)) {                            \
      std::ostringstream _s;                                    \
      _s << msg;                                                \
      logger.write(LEVEL, _s.str(), __FILE__, __LINE__);        \
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
