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
#include <apex/model/Order.hpp>
#include <apex/util/StopFlag.hpp>
#include <apex/util/json.hpp>

namespace apex
{

class RealtimeEventLoop;
class WebsocketClient;

struct Subscription {
  int id;
  bool is_requested = false;
  std::function<std::string(void)> build_request;
  std::function<void(json)> handler;
};

/* Represent an active subscription to Binance account info */
class AccountStream
{
public:
  // using AccountEvent = std::map<std::string, AccountUpdate>;
  using AccountUpdateCallback = std::function<void(std::vector<AccountUpdate>)>;

  AccountStream(AccountUpdateCallback cb) : _callback(std::move(cb)) {}

  void on_update(std::vector<AccountUpdate> updates)
  {
    // Note: not using event dispatch here, since `this` will belong to a
    // BinanceSession instance, ie, we are just a companion class.
    _callback(updates);
  }

private:
  AccountUpdateCallback _callback;
};


class BinanceSession : public ExchangeSession<BinanceSession>
{
public:
  struct Params {
    std::string raw_capture_dir;
    std::string api_key_file;
  };
public:
  BinanceSession(BaseExchangeSession::EventCallbacks, Config& config,
                 RunMode run_mode, IoLoop* ioloop,
                 RealtimeEventLoop& event_loop,
                 SslContext* ssl);
  BinanceSession(BaseExchangeSession::EventCallbacks, Params config,
                 RunMode run_mode, IoLoop* ioloop,
                 RealtimeEventLoop& event_loop,
                 SslContext* ssl);

  void start();

  void subscribe_trades(Symbol, subscription_options,
                        std::function<void(TickTrade)>) override;

  void subscribe_account(
      std::function<void(std::vector<AccountUpdate>)> callback) override;

  void subscribe_top(Symbol, subscription_options,
                     std::function<void(TickTop)>) override;

  void submit_order(OrderParams, SubmitOrderCallbacks) override;

  void cancel_order(std::string symbol, std::string order_id,
                    std::string ext_order_id, SubmitOrderCallbacks) override;

private:
  // void dispatch(std::function<void(BinanceSession* self)>);
  std::shared_ptr<WebsocketClient> open_websocket(std::string, std::string, int,
                                                  std::string,
                                                  std::function<void()>,
                                                  std::function<void(json)>);

  bool eval_connection_state();
  void check_connection_state();

  void manage_connection();

  void ping_market_data();

  void refresh_user_stream_listen_key();

  void refresh_account();

  void http_get_account_information();


  void on_userdata_msg(json);

  std::string endpoint(std::string url);


  // TODO: make remove, since all usage of m_subscriptions appears to be on the
  // event thread?
  std::mutex m_subscriptions_mtx;
  std::map<std::string, Subscription> m_subscriptions;

  std::shared_ptr<WebsocketClient> _mktdata_stream;
  std::shared_ptr<WebsocketClient> _user_stream;

  void on_mktdata_websocket_down();
  void on_websocket_msg(json);
  void on_websocket_up(std::shared_ptr<WebsocketClient>);
  void make_pending_subscriptions();

  void retry_connect_market_data_stream();
  void retry_connect_user_data_stream();

  void http_request(
      HttpRequestType type, std::string endpoint, std::string path,
      std::string post_data, std::vector<std::string> headers,
      std::function<void(std::string reply, std::string error)> on_result);


  void on_user_data_stream_up(std::shared_ptr<WebsocketClient>);
  void on_user_data_stream_reply(json);

  std::mutex _listen_key_lock;
  std::string _listen_key;
  std::chrono::time_point<std::chrono::steady_clock> _listen_key_created;

  void on_account_reply(std::string);
  void on_new_order_reply(std::string, SubmitOrderCallbacks);
  void on_cancel_order_reply(std::string, std::string, SubmitOrderCallbacks);

  std::unique_ptr<AccountStream> _account_stream;

  struct {
    std::string md_host = "stream.binance.com";
    int md_port = 9443;
    std::string md_path = "/stream";

    std::string user_host = "stream.binance.com";
    int user_port = 9443;
    std::string user_path = "/ws";
    int recv_window = 5000;

    std::string api_endpoint = "https://api.binance.com";
    bool use_test = false;
  } _params;

  int _next_id = 1;

  std::string _user_api_key;
  std::string _user_api_secret;

  std::string _raw_capture_dir;

  enum class ServiceState {
    connecting,
    connected,
    reseting
  } _service_state = ServiceState::connecting;
};

void start();

} // namespace apex
