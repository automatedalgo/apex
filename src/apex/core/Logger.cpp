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
#include <apex/core/version.hpp>
#include <apex/util/platform.hpp>
#include <apex/util/utils.hpp>
#include <apex/util/Config.hpp>
#include <apex/util/RealtimeEventLoop.hpp>

#include <functional>
#include <iostream>

// Defined by glibc to provide the program name
extern char *program_invocation_short_name;

namespace apex
{

static constexpr size_t thread_width = 24;

static std::string format_threadid(int thread_id, const std::string& thread_name)
{
  std::array<char, thread_width + 1> buf{}; // +1 for null
  buf.fill(' ');

  std::ostringstream oss;
  oss << "| " << thread_id << "/" << thread_name;
  auto raw = oss.str();

  // copy into the buf
  auto copy_len = std::min(thread_width, raw.length());
  memcpy(buf.data(), raw.data(), copy_len);

  buf[buf.size() - 1] = '\0';
  buf[buf.size() - 2] = ' ';

  return {buf.data(), thread_width};
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
Logger::Logger() : _mask(mask_level_and_above(level::info)) {}

/* Destructor */
Logger::~Logger() = default;

int Logger::mask_level_and_above(level lvl)
{
  return 0 |
    ((level::debug >= lvl) ? level::debug : 0) |
    ((level::info >= lvl) ? level::info : 0) |
    ((level::note >= lvl) ? level::note : 0) |
    ((level::warn >= lvl) ? level::warn : 0) |
    ((level::error >= lvl) ? level::error : 0);
}


void Logger::log_banner(RunMode mode,
                        std::ostream* os) {
  std::string mode_name;
  switch (mode) {
    case RunMode::paper:
      mode_name = "paper trading";
      break;
    case RunMode::live:
      mode_name = "live";
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

    // decide logging handler
    if (!os) {
      if (_async_logfile_opened)
        os = &_async_logfile;
      else
        os = &std::cout;
    }

    for (size_t i = 1; i < banner.size(); i++){
      *os << banner[i];
      if (i == 2)
        *os << "   mode: " << mode_name;
      if (i == 3)
        *os << "    version: " << APEX_VERSION;

      *os << "\n";
    }
    os->flush();
    _banner_done = true;
  }
}


Logger& Logger::instance()
{
  static Logger static_instance;
  return static_instance;
}


void Logger::register_thread_id(std::string label)
{
  auto guard = std::scoped_lock(_thread_ids_mutex);
  auto tid = apex::thread_id();
  _thread_ids[tid] = format_threadid(tid, label);
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


void Logger::configure(level level, bool want_detail)
{
  apex::Logger::instance().set_level(level);
  apex::Logger::instance().set_detail(want_detail);
  apex::Logger::instance().set_is_configured(true); // mark as ready
}


void Logger::configure_from_config(Config config) {
  auto level_str = config.get_string("level", "info");
  auto detailed_logging = config.get_bool("detailed", false);
  auto level = apex::Logger::string_to_level(level_str);
  apex::Logger::instance().set_level(level);
  apex::Logger::instance().set_detail(detailed_logging);
  apex::Logger::instance().set_is_configured(true); // mark as ready
}


void Logger::enable_async_mode() {
  auto guard = std::lock_guard(_async_mtx);
  if (!_async_thread) {
    _async_thread = std::make_unique<RealtimeEventLoop>(
      [](){
        return false;
      },
      [] {
        apex::Logger::instance().register_thread_id("asynclog");
      });
    auto flush_internval = std::chrono::seconds(1);
    _async_thread->dispatch(flush_internval, [flush_internval, this]() {
      this->async_flush();
      return flush_internval;
    });
  }
}


bool Logger::is_async_mode() {
  return _async_thread.get() != nullptr;
}


void Logger::async_flush() {
  if (_async_logfile_opened) {
    _async_logfile.flush();
  }
  else {
    std::cout.flush();
  }
}


void Logger::drain_async_queue()
{
  decltype(_async_queue) q;
  {
    auto guard = std::scoped_lock(_async_mtx);
    q = std::move(_async_queue);
  }

  while (!q.empty()) {
    auto & front = q.front();

    if (_async_logfile_opened) {
      write_to_stream(_async_logfile,
                      front.ts,
                      front.thread_id,
                      front.level,
                      front.message,
                      front.abs_filename,
                      front.lineno);

    }
    else {
      write_to_stream(std::cout,
                      front.ts,
                      front.thread_id,
                      front.level,
                      front.message,
                      front.abs_filename,
                      front.lineno);
    }
    q.pop();
  }
}


void Logger::write(Logger::level level, std::string_view msg,
                   const char* file, int lineno)
{
  auto const tid = apex::thread_id();
  Time log_ts = _clock_fn? _clock_fn() : Time::realtime_now();

  // for non-async logging, need to take the write mutex
  if (is_async_mode()) {
    LogItem item {
      log_ts,
      tid,
      level,
      file,
      lineno,
      std::string(msg)
    };
    auto guard = std::scoped_lock(_async_mtx);
    _async_queue.push(std::move(item));
    _async_thread->dispatch([this](){
      this->drain_async_queue();
    });
  }
  else {
    auto guard = std::scoped_lock(_write_mutex);
    write_to_stream(std::cout,
                    log_ts,
                    tid,
                    level,
                    msg,
                    file,
                    lineno);
  }
}


void Logger::write_to_stream(std::ostream& os,
                             Time log_ts,
                             long thread_id,
                             Logger::level level,
                             std::string_view msg,
                             const char* abs_filename,
                             int lineno)
{
  auto parts = split(abs_filename, '/');
  auto filename = *parts.rbegin();

  /* build the timestamp section of the log entry */

  auto tm = log_ts.tm_utc();
  auto usec = log_ts.usec();
  char tmp[256] = {0};
  snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d | %02d:%02d:%02d.%06lu",
           tm.tm_year+1900,
           tm.tm_mon + 1,
           tm.tm_mday,
           tm.tm_hour,
           tm.tm_min,
           tm.tm_sec,
           usec.count());

  os << tmp << "";

  /* build the thread ID part */

  if (_detailed_logging) {
    auto guard = std::scoped_lock(_thread_ids_mutex);
    if (auto it = _thread_ids.find(thread_id); it == std::end(_thread_ids)) {
      auto label = format_threadid(thread_id, "????");
      os << " " << label;
      _thread_ids[thread_id] = std::move(label);
    } else {
      os << " " << it->second;
    }
  }

  /* log level & message */

  os << " " << level_str(level) << msg;

  /* for detailed logging, add the source code location */

  if (_detailed_logging)
    os << " (" << filename << ":" << lineno << ")";
  os << "\n";
}


std::string generate_auto_log_file_name(const LogOpts &opts)
{
  Time t = Time::realtime_now();
  auto base_path = apex_home() / "log";

  base_path /= std::string(program_invocation_short_name);

  switch (opts.time) {
    case LogOpts::Time::day :
      base_path += t.strftime(".%Y%m%d");
      break;
    case LogOpts::Time::second :
      base_path += t.strftime(".%Y%m%d-%H%M%S");
      break;
    case LogOpts::none:
      break;
  }
  base_path += ".log";

  return base_path.string();
}


void Logger::set_opts(const LogOpts & opts)
{
  set_level(opts.level);
  set_detail(opts.detail);

  if (opts.async)
    enable_async_mode();

  // enable log file
  std::string filename = opts.filename;
  if (!filename.empty()) {
    if (filename == "auto" && opts.auto_filename) {
      filename = generate_auto_log_file_name(opts);
    }
    std::ios_base::openmode mode = (opts.mode == LogOpts::Mode::append) ? std::ios::app : std::ios::trunc;
    _async_logfile.open(std::string(filename), mode);
    if (!_async_logfile.is_open()) {
      std::cerr << "failed to open logflie '" << filename << "'" << std::endl;
      exit(1);
    }
    else {
      _async_logfile_opened = true;
      std::cout << "writing to log file: " << filename << std::endl;
    }
  }
}


} // namespace apex
