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

#include <apex/core/OrderRouterService.hpp>
#include <apex/model/Order.hpp>
#include <apex/util/json.hpp>
#include <apex/venues/venues_common.hpp>

namespace apex {

class OrderRouter;

class BinanceUsdFutLineHandler : public std::enable_shared_from_this<BinanceUsdFutLineHandler>
{
  struct PendReq {
    enum ReqType { session_logon, user_start, user_ping, new_order, cancel_order,  } type;
    std::string order_id;
  };

public:
  BinanceUsdFutLineHandler(Services* services,
                           Reactor* reactor,
                           RealtimeEventLoop* event_loop,
                           LineHandlerCallbacks callbacks,
                           OrderRouterConfig config);

  void start();

  std::shared_ptr<WebsocketClient> open_connection();
  std::shared_ptr<WebsocketClient> connect_user_data_stream();
  void manage_connection();

  void on_line_message(const char*, size_t);
  void on_feed_message(const char*, size_t);

  // TODO: build out the order management API - I want callbacks.  How to we
  // receive a fill?  Think in terms of both embeddecd and via a GX server.
  void submit_order(OrderParams);
  void cancel_order(const MxCancelOrder&);

  void subscribe_user_stream(std::string listenkey);

  void process_session_logon_reply(PendReq&, json&);
  void process_submit_order_reply(PendReq&, json&);
  void process_cancel_order_reply(PendReq&, json&);
  void process_trade_lite(json&);
  void process_order_trade_update(json&);

  void initiate_user_stream();

  void user_stream_keepalive();

  bool is_open() const;

  std::unique_ptr<OrderRouter> get_order_router_adapter();

  Services* _services;
  RealtimeEventLoop* _event_loop;
  Reactor* _reactor;
  SslContext* _ssl;
  LineHandlerCallbacks _callbacks;
  OrderRouterConfig _config;
  std::shared_ptr<WebsocketClient> _ws_line;
  std::shared_ptr<WebsocketClient> _ws_feed;
  std::unique_ptr<RealtimeEventLoop> _connector_thread;

  std::string _listenkey; // mutex?

  int _ws_line_msgcap_id_in;
  int _ws_line_msgcap_id_out;
  int _ws_feed_msgcap_id_in;
  int _ws_feed_msgcap_id_out;

  std::string _line_url;
  std::string _feed_url;

  std::string _apikey;
  std::string _seedhex;

  // used to generate exchange client order Id
  size_t _msg_seq_num = 0;

  // pending array
  std::mutex _pend_mtx;

  std::map<std::string, PendReq> _pend_reqs;
};

}
