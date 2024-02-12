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

#include <apex/model/ExchangeId.hpp>

#include <map>
#include <memory>

namespace apex
{

class GxClientSession;
class Services;
class Config;

class GatewayService
{
public:
  GatewayService(Services*, Config);
  std::shared_ptr<GxClientSession> find_session(ExchangeId);

  void set_default_gateway(std::string port);
  void set_default_gateway(int port);

private:
  Services* _services;
  std::map<ExchangeId, std::shared_ptr<GxClientSession>> _sessions;
  std::shared_ptr<GxClientSession> _default_session;
};

} // namespace apex
