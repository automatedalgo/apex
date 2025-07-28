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

#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/util/json.hpp>
#include <apex/infra/WebsocketClient.hpp>
#include <apex/core/Logger.hpp>
#include <apex/infra/ssl.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace apex;

/*
Example of using websocket class to create a secure websocket-client to an
exchange, subscribe for data, and display received messages.
*/


int run(RealtimeEventLoop& event_loop,
        Reactor& reactor,
        SslContext& ssl_context)
{
  auto host = "fstream.binance.com";
  auto port = 443;
  auto path = "/ws";

  int timeout_secs = 5;

  LOG_INFO("attempting websocket connection to '" << host << ":"
           << port << path << "'");

  /* ----- socket connection ----- */

  auto connected_promise = std::make_shared<std::promise<int>>();
  auto connected_cb = [&](int err) { connected_promise->set_value(err); };

  auto sock = std::make_unique<SslSocket>(&ssl_context, &reactor);
  //auto sock = std::make_unique<TcpSocket>(&reactor);
  sock->connect(host, port, 10, connected_cb);
  auto fut2 = connected_promise->get_future();

  if (fut2.wait_for(std::chrono::seconds(timeout_secs)) != std::future_status::ready)
    throw std::runtime_error("timeout during connect");

  int err2 = fut2.get();
  if (err2)
    throw std::runtime_error("connect failed");

  /* ----- websocket initialisation ----- */

  auto msg_cb = [=](const char* buf, size_t len) {
    LOG_INFO(std::string(buf, len));
  };

  auto websock_promise = std::make_shared<std::promise<void>>();
  auto on_open = [&]{ websock_promise->set_value(); };

  // auto on_error = on_down;
  std::function<void()> on_down = [](){}; // TODO: implement

  LOG_INFO("socket connected: " << sock->is_open());
  sock->start_read([](char* b, ssize_t n){
    LOG_INFO( std::string_view(b,n));
  });

  sleep(20);

  std::shared_ptr<WebsocketClient> ws = std::make_shared<WebsocketClient>(
    event_loop, std::move(sock), path, msg_cb, on_open, on_down);

  {
    // wait for the websocket to become open
    auto fut = websock_promise->get_future();
    if (fut.wait_for(std::chrono::seconds(timeout_secs)) != std::future_status::ready)
      throw std::runtime_error("timeout during websocket initiation");
  }

  if (ws->is_open())
    LOG_INFO("*** websocket open ***");


  std::string msg = R"(
{
  "method": "SUBSCRIBE",
  "params": [
    "btcusdt@aggTrade"
  ],
  "id": 312
}
)";
  ws->send(msg.c_str(), msg.size());

  apex::wait_for_sigint();
  LOG_INFO("control-c detected");

  // ws.sync_close();  // TODO: implement this function
  return 0;
}


int main(int, char**) {

  Logger::instance().set_level(Logger::debug);
  Logger::instance().set_detail(true);
  apex::Logger::instance().register_thread_id("main");

  std::function<bool()> on_event_exception = [](){
    LOG_INFO("got exception");
    return true;
  };

  RealtimeEventLoop event_loop(on_event_exception);

  Reactor reactor;
  SslConfig config(true);
  config.security_level = 0;
  SslContext ssl_context(config);

  int ec = run(event_loop, reactor, ssl_context);


  return ec;
}
