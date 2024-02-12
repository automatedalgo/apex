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

#include <apex/util/StopFlag.hpp>

namespace apex {


StopFlag::StopFlag()
  : _is_requested(false), _has_stopped_fut(_has_stopped.get_future())
{
}


void StopFlag::request_stop()
{
  std::unique_lock<std::mutex> lock(_mutex);
  _is_requested = true;
  _condition.notify_all();
}


bool StopFlag::is_requested() const
{
  std::unique_lock<std::mutex> lock(_mutex);
  return _is_requested;
}


void StopFlag::set_has_stopped()
{ _has_stopped.set_value();
}

std::shared_future<void> StopFlag::has_stopped_future()  const
{
  return _has_stopped_fut;
}


void StopFlag::wait_for_requested(std::chrono::seconds duration)
{
  std::unique_lock<std::mutex> lock(_mutex);
  _condition.wait_for(lock, duration,
                       [&]{ return _is_requested; } );
}

}
