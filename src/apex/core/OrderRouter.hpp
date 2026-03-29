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

#include <apex/model/model_common.hpp>

#include <memory>
#include <string>

namespace apex
{

class Core;
class GxClientSession;
class Order;
class OrderRouter;
class RealtimeOrderRouter;


class OrderRouter {
public:
  virtual ~OrderRouter() = default;
  virtual SendStatus send_order(Order&) = 0;
  virtual SendStatus cancel_order(Order&) = 0;
  virtual bool is_up() const = 0;
};

class RealtimeOrderRouter : public OrderRouter
{
public:
  RealtimeOrderRouter(apex::Core* core,
                      std::shared_ptr<GxClientSession> gx_session,
                      std::string strategy_id);

  SendStatus send_order(Order&) override;
  SendStatus cancel_order(Order&) override;
  bool is_up() const override;

private:
  std::shared_ptr<GxClientSession>& gx_session();
  Core* _core;
  std::shared_ptr<GxClientSession> _gx_session;
  std::string _strategy_id;
  bool _is_up = false;
};

} // namespace apex
