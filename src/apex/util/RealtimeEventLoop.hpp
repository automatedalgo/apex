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

#include <apex/util/utils.hpp>
#include <apex/util/EventLoop.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace apex
{

struct Event;


/** Event thread */
class RealtimeEventLoop : public EventLoop
{
public:
  explicit RealtimeEventLoop(std::function<bool()> on_exception,
            std::function<void()> on_start = {},
            std::function<void()> on_stop = {});
  RealtimeEventLoop(const RealtimeEventLoop&) = delete;
  RealtimeEventLoop& operator=(const RealtimeEventLoop&) = delete;
  ~RealtimeEventLoop();

  /** Perform synchronous stop of the event loop.  On return, the EV thread will
   * have been joined. */
  void sync_stop() override;

  /** Post a function object that is later invoked on the event thread. */
  void dispatch(std::function<void()> fn) override;

  /** Post a timer_fn function which is invoked after the elapsed time. The
   * function will be repeatedly invoked until it returns 0. */
  void dispatch(std::chrono::milliseconds, timer_fn fn) override;

  /** Determine whether the current thread is the EV thread. */
  bool this_thread_is_ev()  const override;

private:
  void handle_exception();
  void eventloop();
  void eventmain();

  void dispatch(std::chrono::milliseconds, std::shared_ptr<Event>);

  std::function<bool()> m_on_exception;
  std::function<void()> m_on_start;
  std::function<void()> m_on_stop;

  bool m_continue;

  std::list<std::shared_ptr<Event>> m_queue;
  std::mutex m_mutex;
  std::condition_variable m_condvar;
  std::multimap<std::chrono::steady_clock::time_point, std::shared_ptr<Event>>
      m_schedule;

  synchronized_optional<std::thread::id> m_thread_id;

  std::thread m_thread; // prefer as final member, avoid race conditions
};

} // namespace apex
