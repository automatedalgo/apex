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

#include <apex/net/WebsocketClient.hpp>
#include <apex/venues/venues_common.hpp>
#include <apex/core/Services.hpp>
#include <apex/util/EventLoop.hpp>
#include <apex/core/Errors.hpp>

namespace apex {


bool websock_is_open(const std::shared_ptr<WebsocketClient>& ws)
{
  return ws && ws->is_open();
}


void OrderRouterAdapterImpl::check_is_up(Order& order)
{
  // check is up?
  if (!this->is_up()) {
    std::weak_ptr<Order> wp = order.weak_from_this();
    _services->evloop()->dispatch([wp]() {
      if (auto sp = wp.lock())
        sp->set_is_rejected(error::e0002, "exchange link down");
    });
    return;
  }
}


} // namespace
