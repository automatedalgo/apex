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

#include <apex/util/RealtimeEventLoop.hpp>

#include <iostream>

namespace apex
{

struct Event {
  enum event_type { kill = 0, function_dispatch, timer_dispatch } type;
  explicit Event(event_type t) : type(t) {}
  virtual ~Event() = default;
};


struct ev_function_dispatch : Event {
  ev_function_dispatch(std::function<void()> fn_)
    : Event(Event::function_dispatch), fn(std::move(fn_))
  {
  }

  std::function<void()> fn;
};


struct ev_timer_dispatch : Event {
  ev_timer_dispatch(RealtimeEventLoop::timer_fn fn_)
    : Event(Event::timer_dispatch), fn(std::move(fn_))
  {
  }

  RealtimeEventLoop::timer_fn fn;
};


RealtimeEventLoop::RealtimeEventLoop(
    std::function<bool()> on_exception,
    std::function<void()> on_start,
    std::function<void()> on_stop)
  : m_on_exception(on_exception),
    m_on_start(std::move(on_start)),
    m_on_stop(std::move(on_stop)),
    m_continue(true),
    m_thread(&RealtimeEventLoop::eventmain, this)
{
}


RealtimeEventLoop::~RealtimeEventLoop() { sync_stop(); }


void RealtimeEventLoop::sync_stop()
{
  auto kill_event = std::make_shared<Event>(Event::kill);

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_queue.push_back(std::move(kill_event));
    m_condvar.notify_one();
  }

  if (m_thread.joinable())
    m_thread.join();
}


void RealtimeEventLoop::dispatch(std::function<void()> fn)
{
  auto event = std::make_shared<ev_function_dispatch>(std::move(fn));

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_queue.push_back(std::move(event));
    m_condvar.notify_one();
  }
}


void RealtimeEventLoop::dispatch(std::chrono::milliseconds delay, timer_fn fn)
{
  dispatch(delay, std::make_shared<ev_timer_dispatch>(std::move(fn)));
}


void RealtimeEventLoop::dispatch(std::chrono::milliseconds delay,
                         std::shared_ptr<Event> sp)
{
  auto tp_due = std::chrono::steady_clock::now() + delay;
  auto event = std::make_pair(tp_due, std::move(sp));

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_schedule.insert(std::move(event));
    m_condvar.notify_one();
  }
}


void RealtimeEventLoop::eventloop()
{
  /* A note on memory management of the event objects.  Once they are pushed,
   * they are stored as shared pointers.  This allows other parts of the code to
   * take ownership of the resource, if they so wish.
   */
  std::list<std::shared_ptr<Event>> to_process;
  while (m_continue) {
    to_process.clear();
    {
      std::unique_lock<std::mutex> guard(m_mutex);

      while (m_continue && m_queue.empty()) {

        // identify range of scheduled events which are now due
        const auto tp_now = std::chrono::steady_clock::now();
        const auto upper_iter = m_schedule.upper_bound(tp_now);

        if (upper_iter == m_schedule.begin()) {
          // no events due now so need to sleep, which is either indefinitely or
          // until the next scheduled item
          if (m_schedule.empty())
            m_condvar.wait(guard);
          else {
            auto sleep_for = m_schedule.begin()->first - tp_now;
            m_condvar.wait_for(guard, sleep_for);
          }
        } else {
          for (auto iter = m_schedule.begin(); iter != upper_iter; ++iter)
            m_queue.push_back(std::move(iter->second));
          m_schedule.erase(m_schedule.begin(), upper_iter);
        }
      }
      to_process.swap(m_queue);
    }

    for (auto& ev : to_process) {
      if (m_continue)  // always recheck, just in case set in handle_exception
        try {
          switch (ev->type) {
            case Event::function_dispatch: {
              ev_function_dispatch* ev2 =
                dynamic_cast<ev_function_dispatch*>(ev.get());
              ev2->fn();
              break;
            }
            case Event::timer_dispatch: {
              ev_timer_dispatch* ev2 = dynamic_cast<ev_timer_dispatch*>(ev.get());
              auto repeat_ms = ev2->fn();
              if (repeat_ms.count() > 0)
                dispatch(repeat_ms, std::move(ev));
              break;
            }
            case Event::kill: {
              m_continue = false;
              return;
            }
          }
        } catch (...) {
          handle_exception();
        }
    } // loop end
  }
}


void RealtimeEventLoop::handle_exception()
{
  try {
    if (!m_on_exception)
      return;

    bool terminate_loop = m_on_exception();
    if (terminate_loop)
      m_continue = true;
  }
  catch (...) {
    m_continue = true;
  }
}


/* Thread entry point */
void RealtimeEventLoop::eventmain()
{
  scope_guard undo_thread_id([this]() { m_thread_id.release(); });

  m_thread_id.set_value(std::this_thread::get_id());

  if (m_on_start)
    try {
      m_on_start();
    } catch (...) {
      handle_exception();
    }

  while (m_continue) {
    try {
      eventloop();
    } catch (...) {
      handle_exception();
    }
  }

  if (m_on_stop)
    try {
      m_on_stop();
    } catch (...) {
      handle_exception();
    }
}


bool RealtimeEventLoop::this_thread_is_ev() const
{
  return m_thread_id.compare(std::this_thread::get_id());
}

} // namespace apex
