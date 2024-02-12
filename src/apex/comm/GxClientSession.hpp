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

#include <apex/comm/GxSessionBase.hpp>
#include <apex/core/Services.hpp>
#include <apex/model/ExchangeId.hpp>
#include <apex/util/Time.hpp>
#include <apex/util/rx.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <variant>

namespace apex
{

class TcpSocket;
class Account;
class Order;
class MarketData;
class AccountUpdate;
class OrderFill;
class OrderService;

/*
 * Provide a GX session used by a client application, eg, a trading engine, to
 * connect to a GX server.
 */
class GxClientSession : public GxSessionBase<GxClientSession>
{

  struct MarketViewSubscription {
    std::string symbol;
    ExchangeId exchange;
    MarketData* mv;
  };

  struct AccountSubscription {
    std::string exchange;
    Account* model;
  };

  typedef std::variant<MarketViewSubscription, AccountSubscription>
      VariantSubscription;


public:
  GxClientSession(IoLoop& ioloop, RealtimeEventLoop& evloop, std::string addr,
                  std::string port, OrderService*);

  void strategy_logon(std::string strategy_id, RunMode run_mode);

  void subscribe(std::string symbol, ExchangeId exchange, apex::MarketData* mv);

  void subscribe_account(std::string exchange, Account& target);

  void start_connecting();

  void new_order(Order&);
  void cancel_order(Order&);

  bool is_connected();

  rx::observable<bool>& connected_observable();
  rx::observable<std::string>& om_logon_observable();

protected:
  void check_connection();

  void perform_subscriptions();

  void io_on_full_message(gx::Header* header, char* payload,
                          size_t payload_len) override;

  void on_submit_order_error(gx::t_msgid, std::string code, std::string text);
  void on_cancel_order_error(gx::t_msgid, std::string code, std::string text);

  uint32_t _next_reqid = 1;

  std::string _remote_addr;
  std::string _remote_port;

  std::unique_ptr<TcpSocket> _pending_sock;
  Time _pending_sock_start;
  std::future<UvErr> _pending_sock_fut;

  // TODO- tidy up these containers, the pending need to all go into a
  // variant.

  // Pending requests
  std::map<gx::t_msgid, std::weak_ptr<Order>> _pending_submit_order;
  std::map<gx::t_msgid, std::weak_ptr<Order>> _pending_cancel_order;

  // Pending subscriptions
  std::vector<MarketViewSubscription> _pending_subs;
  std::map<std::string, MarketViewSubscription> _active_subs;
  std::vector<VariantSubscription> _pending_subs_2;

  // Type based active subscriptions
  std::map<std::string, apex::MarketData*> _ticks_subscription;
  std::map<std::string, AccountSubscription> _account_subs;

  OrderService* _order_service;

  rx::behaviour_subject<bool> m_connected_subject;
  rx::subject<std::string> m_om_logon_subject;
};
} // namespace apex
