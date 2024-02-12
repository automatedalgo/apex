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

#include <apex/core/OrderRouter.hpp>
#include <apex/core/Errors.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/model/Order.hpp>


namespace apex
{

RealtimeOrderRouter::RealtimeOrderRouter(apex::Services* services,
                                         std::shared_ptr<GxClientSession> gx_session,
                     std::string strategy_id)
  : _services(services),
    _gx_session(std::move(gx_session)),
    _strategy_id(strategy_id)
{

  auto wp = _gx_session->weak_from_this();

  // TODO: OmSession needs to be a shared_from_this, as can see here, we are
  // capturing a this pointer.
  _gx_session->om_logon_observable().subscribe(
      [strategy_id, this](std::string error) {
        if (!error.empty()) {
          this->_is_up = false;
          LOG_ERROR("error attempting apex-gx strategy logon: " << error);
        } else {
          this->_is_up = true;
          LOG_INFO("order-router logon successful for strategyId "
                   << QUOTE(strategy_id));
        }
      });

  _gx_session->connected_observable().subscribe(
      [wp, strategy_id, services, this](const bool& is_up) {
        if (!is_up)
          this->_is_up = false;
        if (auto sp = wp.lock()) {
          if (is_up) {
            sp->strategy_logon(strategy_id, services->run_mode());
          }
        }
      });
}

std::shared_ptr<GxClientSession>& RealtimeOrderRouter::gx_session()
{
  return _gx_session;
}


void RealtimeOrderRouter::send_order(Order& order)
{
  if (!is_up()) {
    std::weak_ptr<Order> wp = order.weak_from_this();
    _services->evloop()->dispatch([wp]() {
      if (auto sp = wp.lock())
        sp->set_is_rejected(error::e0003, "gx not connected");
    });
  } else {
    _gx_session->new_order(order);
  }
}

void RealtimeOrderRouter::cancel_order(Order& order)
{
  if (_gx_session->is_connected()) {
    _gx_session->cancel_order(order);
  }
  else {
    LOG_ERROR("cannot cancel order " << order.order_id() << " for " << order.ticker() <<"; GX connection down");
    throw std::runtime_error("cannot cancel order");
  }
}

bool RealtimeOrderRouter::is_up() const { return _is_up; }


} // namespace apex
