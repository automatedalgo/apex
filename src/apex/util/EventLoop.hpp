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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <stdexcept>

namespace apex
{

/* Base class for event-loop implementations. */
class EventLoop
{
public:
  /* Signature for timer callbacks that can be registered with the event loop.
   * The return value indicates the delay to use for subsequent invocation of
   * the timer function, or 0 if the function should not be invoked again. */
  typedef std::function<std::chrono::milliseconds()> timer_fn;

  virtual ~EventLoop() {}

  /** Post a function object that is later invoked on the event thread. */
  virtual void dispatch(std::function<void()> fn) = 0;

  /** Post a timer function which is invoked after the elapsed time. */
  virtual void dispatch(std::chrono::milliseconds, timer_fn fn) = 0;

  virtual void sync_stop() {};

  virtual bool this_thread_is_ev() const = 0;
};

}
