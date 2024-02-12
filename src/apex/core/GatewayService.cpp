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

#include <apex/core/GatewayService.hpp>
#include <apex/util/Config.hpp>
#include <apex/comm/GxClientSession.hpp>
#include <apex/core/Services.hpp>

namespace apex
{

GatewayService::GatewayService(Services* services, Config config)
  : _services(services)
{
  if (config.is_empty()) {
    LOG_WARN("no gateways configured");
    return;
  }

  // construct all gateway sessions, based on config
  for (size_t i = 0; i < config.array_size(); i++) {
    auto gateway_config = config.array_item(i);
    std::string node = gateway_config.get_string("host");
    std::string port = gateway_config.get_string("port");
    auto provides = gateway_config.get_string("provides");

    auto provides_exchange_id = to_exchange_id(provides);

    auto iter = _sessions.find(provides_exchange_id);
    if (iter != std::end(_sessions)) {
      std::ostringstream oss;
      oss << "multiple gateways configured for exchange " << QUOTE(provides);
      throw ConfigError(oss.str());
    }
    auto session = std::make_shared<apex::GxClientSession>(
        *services->ioloop(), *services->realtime_evloop(), node, port,
        services->order_service());

    session->start_connecting();
    _sessions[provides_exchange_id] = std::move(session);
  }
}


std::shared_ptr<GxClientSession> GatewayService::find_session(
    ExchangeId exchange)
{
  auto iter = _sessions.find(exchange);
  if (iter != _sessions.end())
    return iter->second;
  else
    return _default_session;
}


void GatewayService::set_default_gateway(std::string port) {
  _default_session  =  std::make_shared<apex::GxClientSession>(
      *_services->ioloop(), *_services->realtime_evloop(), "127.0.0.1", port,
      _services->order_service());

  _default_session->start_connecting();
}


void GatewayService::set_default_gateway(int port) {
  std::ostringstream oss;
  oss << port;
  return this->set_default_gateway(oss.str());
}

} // namespace apex
