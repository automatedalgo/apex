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

#include <apex/util/BacktestEventLoop.hpp>
#include <apex/util/utils.hpp>
#include <apex/core/Logger.hpp>

#include <cassert>

namespace apex
{

// This class is only used by BacktestEventLoop, so is placed only with this .cc
// file
class BacktestTimers : public BacktestEventSource {
public:

  void init_backtest_time_range(Time , Time ) override
  {
    // TODO: integrate these limits with the on the fly timer
  }

  Time get_next_event_time() override
  {
    if (std::size(m_timers))
      return m_timers.begin()->first;
    else
      return {};
  }

  void consume_next_event() override
  {
    // this assert ensures that the backtest event loop only asks for
    // an event if one is infact due
    assert(std::empty(m_timers) == false);

    auto iter = m_timers.begin();

    // invoke the callback function, which can return a time-interval to reset
    // the timer
    std::chrono::milliseconds reset_interval = iter->second();

    // TODO: in a multimap, is this the correct thing to do?

    if (reset_interval.count() != 0) {
      Time current = iter->first;
      EventLoop::timer_fn fn = std::move(iter->second);
      m_timers.erase(iter);
      schedule_timer(current, reset_interval, std::move(fn));
    }
    else {
      m_timers.erase(iter);
    }
  }


  void add_timer(Time current,
                 std::chrono::milliseconds interval,
                 EventLoop::timer_fn fn)
  {
    if (current.empty())
      m_pending_timers.push_back({interval, fn});
    else
      schedule_timer(current, interval, fn);
  };


  void schedule_timer(Time current,
                      std::chrono::milliseconds interval,
                      EventLoop::timer_fn fn)
  {
    auto due = current;
    due += interval;
    m_timers.insert({due, fn});
  }

  void schedule_pending_timers(Time current)
  {
    for (auto & item: m_pending_timers)
      schedule_timer(current, item.interval, item.fn);
    m_pending_timers.clear();
  }


private:
  std::multimap<Time, EventLoop::timer_fn> m_timers;

  struct PendingTimer {
    std::chrono::milliseconds interval;
    EventLoop::timer_fn fn;
  };

  std::vector<PendingTimer> m_pending_timers; // not scheduled yet
};


BacktestEventLoop::BacktestEventLoop(Time backtest_time_start)
  : _current(backtest_time_start)
{
  _timers.reset(new BacktestTimers);
  _sources.push_back(_timers.get());
}


BacktestEventLoop::~BacktestEventLoop()
{
}


void BacktestEventLoop::add_event_source(BacktestEventSource* source)
{
  _sources.push_back(source);
}


void BacktestEventLoop::add_event_sources(std::vector< std::shared_ptr<BacktestEventSource>> sources)
{
  for (auto & item : sources)
    _sp_sources.push_back(std::move(item));

}


void BacktestEventLoop::dispatch(std::chrono::milliseconds interval,
                                 EventLoop::timer_fn fn)
{
  _timers->add_timer(_current, interval, fn);
}


void BacktestEventLoop::dispatch(std::function<void()> fn)
{
  // to implement the dispatch of an immediate callback, we just create a 1
  // millisecond timer.

  _timers->add_timer(_current, std::chrono::milliseconds(1),
                      [fn]() -> std::chrono::milliseconds {
                        fn();
                        return {};
                      });
}


std::pair<Time, BacktestEventSource*> BacktestEventLoop::find_earliest()
{
  std::pair<Time, BacktestEventSource*> earliest {{},nullptr};

  // TODO: these two sections are (almost) identical, so need to find a way to combine
  // them, easiest would be just to reply on shared pointer.
  for (auto p : _sources) {
    auto t = p->get_next_event_time();
    if (!t.empty() &&
        (t < earliest.first || earliest.first.empty())) {
      earliest = {t, p};
    }
  }
  for (auto p : _sp_sources) {
    auto t = p->get_next_event_time();
    if (!t.empty() &&
        (t < earliest.first || earliest.first.empty())) {
      earliest = {t, p.get()};
    }
  }


  return earliest;
}


void BacktestEventLoop::update_current_time(Time t)
{
  if (_current != t) {
    if (_current.empty()) {
      LOG_INFO("setting backtest from-time to " << t.as_iso8601());
      _timers->schedule_pending_timers(t);
    }
    else if (t < _current) {
      LOG_WARN("attempt to set backtest time backwards, from now: "
               << _current << ", to: " << t);
      throw std::runtime_error("backtest time cannot go backwards");
    }

    _current = t;
  }
}


void BacktestEventLoop::run_loop(Time upto)
{
  int event_count = 0;
  for (auto p : _sources)
    p->init_backtest_time_range(_from, upto);
  for (auto p : _sp_sources)
    p->init_backtest_time_range(_from, upto);

  //if (!m_current.empty())
  //  throw std::runtime_error("backtest loop already run");

  if (_from.empty())
    update_current_time(find_earliest().first);
  else
    update_current_time(_from);


  LOG_INFO("starting backtest event loop");
  while (true) {
    try {
      // find source that has next evet
      const auto [next_time, next_source] = find_earliest();

      if (next_source != nullptr)
      {
        update_current_time(next_time);
        event_count ++;
        next_source->consume_next_event();
      }
      else {
        LOG_INFO("backtest ran out of data");
        break;
      }

      if (!upto.empty() && upto < _current) {
        LOG_INFO("backtest reached end time -- backtest complete");
        break;
      }
    }
    catch (const std::exception & e) {
      LOG_ERROR("caught exception at backtest event loop: (" << demangle(typeid(e).name()) << ") " << e.what());
      return; /* terminate event loop */
    }
    catch (...) {
      LOG_ERROR("caught unknown exception at backtest event loop");
      return; /* terminate event loop */
    }
  }
}


void BacktestEventLoop::set_time(Time start)
{
  _from = start;
}

}
