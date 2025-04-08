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

#include <apex/gx/ExchangeSession.hpp>
#include <apex/gx/BinanceSession.hpp>
#include <apex/model/MarketData.hpp>
#include <apex/comm/GxServerSession.hpp>
#include <apex/infra/ssl.hpp>

#include <memory>
#include <utility>

namespace apex {

struct ExchangeSubscriptionKey {
  ExchangeId exchange_id;
  std::string symbol;

  bool operator<(const ExchangeSubscriptionKey& rhs) const
  {
    return (exchange_id < rhs.exchange_id) ||
      (exchange_id == rhs.exchange_id && symbol < rhs.symbol);
  }
};

/* Represent a single stream subscription on an exchange a set of associated
 * targets that shall receive updates. */
class ExchangeSubscription
  : public std::enable_shared_from_this<ExchangeSubscription>
{
public:
  std::weak_ptr<ExchangeSubscription> get_weak()
  {
    return this->weak_from_this();
  }

  ExchangeSubscription(
    std::shared_ptr<apex::BaseExchangeSession> exchange_session,
    ExchangeSubscriptionKey sym);

  void activate();
  void subscribe(GxServerSession& session);

private:
  std::shared_ptr<apex::BaseExchangeSession> _exchange_session;
  ExchangeSubscriptionKey _symbol;
  std::vector<std::shared_ptr<GxServerSession>> _subscribers;
  MarketData _market;
};


struct ExchangeId2 {
  std::string name;

  explicit ExchangeId2(std::string exchange_name)
    : name(std::move(exchange_name))
  {
  }

  bool operator<(const ExchangeId2& other) const
  {
    return this->name < other.name;
  }
};


class Topic
{
public:
  void add_subscriber(std::shared_ptr<GxServerSession> sp)
  {
    _subscribers.push_back(std::move(sp));
  }

  std::vector<std::shared_ptr<GxServerSession>> _subscribers;
};


class AccountTopic : public Topic
{
public:
  void start_exchange_subscription(
    std::shared_ptr<apex::BaseExchangeSession> session);

  Account& model() { return _model; }

private:
  Account _model;
  std::shared_ptr<apex::BaseExchangeSession> _exchange_session;
};


class GxServer
{
public:
  explicit GxServer(apex::RunMode run_mode,
                    apex::Config config);

  explicit GxServer(RealtimeEventLoop* external_event_loop,
                    apex::RunMode run_mode,
                    apex::Config config = Config::empty_config());

  ~GxServer();

  void start();

  void add_venue(BinanceSession::Params);

  int get_listen_port() const { return _port; }

private:
  void new_client(std::shared_ptr<GxServerSession>);

  // event handlers for GX-session events
  void on_error(GxServerSession&);

  void on_subscribe(GxServerSession&, GxSubscribeRequest&);

  void on_cancel_order_request(GxServerSession&, GxServerSession::Request,
                               ExchangeId exchange, std::string symbol,
                               std::string order_id, std::string ext_ord);

  bool on_logon_request(GxServerSession&, std::string, RunMode);

  void on_fill(BaseExchangeSession&, std::string order_id, OrderFill);
  void on_unsol_cancel(BaseExchangeSession&, std::string order_id, OrderUpdate);

  int create_listen_socket();

  RealtimeEventLoop* event_loop();

  RunMode _run_mode;
  Config _config;


  std::unique_ptr<apex::RealtimeEventLoop> _own_event_loop;
  apex::RealtimeEventLoop* _external_event_loop;

  apex::Reactor _reactor;
  std::unique_ptr<apex::SslContext> _ssl;

  // exchange connections
  std::map<ExchangeId, std::shared_ptr<apex::BaseExchangeSession>>
  _exchange_sessions;

  // exchange subscriptions
  std::map<ExchangeSubscriptionKey, std::shared_ptr<ExchangeSubscription>>
  _exchange_subscriptions;

  // for embedded mode, convenient to allow listen port discovery
  bool _try_other_ports = false;

  // port & socket for accepting new GX-sessions
  int _port;
  std::unique_ptr<TcpSocket> _server_sock;

  // GX-sessions
  std::vector<std::shared_ptr<GxServerSession>> _gx_sessions;

  //void on_subscribe_wallet(GxServerSession& session, std::string);

  std::map<ExchangeId2, std::unique_ptr<AccountTopic>> _wallets;
  void on_submit_order(GxServerSession&, GxServerSession::Request,
                       OrderParams&);

  std::map<std::string, std::shared_ptr<GxServerSession>> _gx_session_map;
};



} // namespace apex
