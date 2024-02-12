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

#include <memory>
#include <string>

namespace apex
{
class Services;
class GxClientSession;
class Order;

class OrderRouter;
class RealtimeOrderRouter;

class OmService
{

public:
  // PriceFeed
  // OrderGateway
  RealtimeOrderRouter* get_order_session(const std::string& exchange,
                                         const std::string& strategy_id);

private:

  // same combinations of exchange & strategy_id

};


class OrderRouter {
public:
  virtual void send_order(Order&) = 0;
  virtual void cancel_order(Order&) = 0;
  virtual bool is_up() const = 0;
};

class RealtimeOrderRouter : public OrderRouter // TODO: use shared_ptr, due to event based callbacks
{
public:
  RealtimeOrderRouter(apex::Services* services,
                      std::shared_ptr<GxClientSession> gx_session,
                      std::string strategy_id);


  void send_order(Order&) override;
  void cancel_order(Order&) override;
  bool is_up() const override;

private:
  std::shared_ptr<GxClientSession>& gx_session();
  Services* _services;
  std::shared_ptr<GxClientSession> _gx_session;
  std::string _strategy_id;
  bool _is_up = false;
};

} // namespace apex
