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

#include <apex/model/Order.hpp>
#include <apex/venues/venues_common.hpp>

namespace apex {

class WebsocketClient;

class BinanceUsdFutFeedHandler : public FeedHandlerImpl<BinanceUsdFutFeedHandler>
{
public:
  constexpr static std::string_view feed_id = "binance_usdfut";

  BinanceUsdFutFeedHandler(Core* core,
                           RunMode run_mode,
                           Reactor* reactor,
                           RealtimeEventLoop* event_loop,
                           FeedHandlerCallbacks);
  ~BinanceUsdFutFeedHandler();
  void start() override;
  void subscribe_trades(std::string) override;
  void subscribe_top(std::string) override;

private:

  void process_raw_message(const char*, size_t);
  void manage_connection();
  void do_subscriptions();

  FeedHandlerCallbacks _callbacks;
  std::shared_ptr<WebsocketClient> _ws_feed;
  // TODO: move into the base class?
  std::unique_ptr<RealtimeEventLoop> _connector_thread;
  std::string _feed_url;
  struct Subscription {
    int id;
    std::string request;
    bool active;
  };
  std::mutex _subs_mtx;
  std::map<std::string, Subscription> _subs;
  int _ws_msgcap_id_in;
  int _ws_msgcap_id_out;

  Time _time_last_ping;
struct ParserImpl;
  std::unique_ptr<ParserImpl> _impl;
};

} //namespace
