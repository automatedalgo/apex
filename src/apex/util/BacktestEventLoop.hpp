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

#include <apex/util/EventLoop.hpp>
#include <apex/util/Time.hpp>

namespace apex
{

class BacktestTimers;


class BacktestEventSource {
public:
  virtual Time get_next_event_time() = 0;
  virtual void consume_next_event() = 0;
  virtual void init_backtest_time_range(Time start, Time end) = 0;
  virtual ~BacktestEventSource() {}
};


class BacktestEventLoop : public EventLoop
{
public:
  explicit BacktestEventLoop(Time backtest_time_start);
  ~BacktestEventLoop();

  void dispatch(std::function<void()> fn) override;
  void dispatch(std::chrono::milliseconds interval,
                EventLoop::timer_fn fn) override;

  Time get_time() const { return _current; }
  void add_event_source(BacktestEventSource* source);
  void add_event_sources(std::vector< std::shared_ptr<BacktestEventSource>>);
  void run_loop(Time upto);

  void set_time(Time start);

  BacktestEventLoop(const BacktestEventLoop&) = delete;
  BacktestEventLoop& operator=(const BacktestEventLoop&) = delete;

  bool this_thread_is_ev() const override { return true; }

private:
  std::pair<Time, BacktestEventSource*> find_earliest();
  void update_current_time(Time t);

  std::vector<BacktestEventSource*> _sources;
  std::vector<std::shared_ptr<BacktestEventSource>> _sp_sources;
  Time _current;
  std::unique_ptr<BacktestTimers> _timers;
  Time _from;
};

}
