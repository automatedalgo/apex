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

#include <apex/core/Services.hpp>
#include <apex/comm/GxSessionBase.hpp>
#include <apex/model/Order.hpp>
#include <apex/model/Instrument.hpp>
#include <apex/model/tick_msgs.hpp>

namespace apex
{
class UvErr;
class TickTrade;
class TickTop;
class AccountUpdate;
class Reactor;

namespace pb
{
class SubscribeTicks;
}

struct GxSubscribeRequest {
  std::string symbol;
  ExchangeId exchange;

  GxSubscribeRequest(std::string symbol,
                     ExchangeId exchange)
    : symbol(std::move(symbol)),
      exchange(exchange) {
  }
};

struct GxLogonRequest {
  std::string strategy_id;
  RunMode run_mode;
};

/* Provide a GX session as used by a GX server application,
 * such as the GX gateway application.  */
class GxServerSession : public GxSessionBase<GxServerSession>
{

public:
  struct Request {
    gx::Type req_type;
    gx::t_msgid req_id;
  };

  struct EventHandlers {
    std::function<void(GxServerSession&)> on_err;
    std::function<void(GxServerSession&, GxSubscribeRequest&)> on_subscribe;
    std::function<void(GxServerSession&, ExchangeId)>
        on_subscribe_account;
    std::function<void(GxServerSession&, Request, OrderParams)> on_submit_order;
    std::function<void(GxServerSession&, Request, ExchangeId,
                       std::string symbol, std::string order_id,
                       std::string ext_order_id)>
        on_cancel_order_request;
    std::function<bool(GxServerSession&, GxLogonRequest)> on_logon;
  };

  GxServerSession(Reactor* reactor, RealtimeEventLoop& evloop, std::unique_ptr<TcpSocket>,
                  EventHandlers);

  ~GxServerSession();

  void send(const std::string & native_symbol,
            ExchangeId exchange_id, TickTrade&);

  void send(const std::string & symbol, ExchangeId, TickTop&);

  void send(ExchangeId, const std::vector<AccountUpdate>&);

  // Reply to a previous request with an error result
  void send_error(Request, std::string code, std::string error);

  void send(Request, OrderUpdate&);

  void send_order_fill(ExchangeId exchange_id,
                       const std::string& order_id, const OrderFill& fill);

  void send_order_unsol_cancel(ExchangeId exchange_id,
                               const std::string& order_id, const OrderUpdate&);

  void send_logon_reply(std::string error = "");
  void send_om_logon_reply(std::string error = "");

  void set_app_id(std::string);

private:
  void on_subscribe(const apex::pb::SubscribeTicks&);

  void io_on_full_message(gx::Header* header, char* payload,
                          size_t payload_len) override;

  EventHandlers _server_callbacks;
  std::string _app_id;

  bool _logon_accepted = false;
};

} // namespace apex
