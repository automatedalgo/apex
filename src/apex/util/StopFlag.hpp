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

#include <future>

namespace apex {
  class StopFlag {
  public:
    StopFlag();

    StopFlag(const StopFlag &) = delete;

    void request_stop();

    bool is_requested() const;

    void set_has_stopped();

    std::shared_future<void> has_stopped_future() const;

    void wait_for_requested(std::chrono::seconds);

  private:
    mutable std::mutex _mutex;
    std::condition_variable _condition;
    bool _is_requested;
    std::promise<void> _has_stopped;
    std::shared_future<void> _has_stopped_fut; //  <--- init this
  };

}
