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
#include <apex/infra/IoLoop.hpp>
#include <apex/infra/ssl.hpp>

#include <chrono>

using namespace std::chrono_literals;
using namespace apex;

/*
Example of using websocket class to create a secure websocket-client to an
exchange, subscribe for data, and display received messages.
*/


int run(RealtimeEventLoop& event_loop,
        IoLoop& io_loop,
        SslContext& ssl_context)
{
  auto address = "testnet.binance.vision";
  auto port = 443;
  auto path = "/ws";

  auto sock = std::make_unique<SslSocket>(ssl_context, io_loop);

  LOG_INFO("connecting...");
  auto fut = sock->connect(address, port);

  if (fut.wait_for(5s) != std::future_status::ready) {
    LOG_ERROR("connect timeout");
    return 1;
  }

  auto err = fut.get();

  if (err) {
    LOG_ERROR("connect failed: " << err);
    return 1;
  }
  else {
    LOG_INFO("connect success");
  }


  WebsocketClient ws(event_loop,
                     std::move(sock),
                     path,
                     [](const char* buf, size_t len){
                       std::string s(buf, len);
                       std::cout << s << "\n";
                     },
                     std::function<void()>{},
                     std::function<void()>{});

  std::string msg = R"(
{
  "method": "SUBSCRIBE",
  "params": [
    "btcusdt@depth"
  ],
  "id": 312
}
)";
  ws.send(msg.c_str(), msg.size());

  apex::wait_for_sigint();
  LOG_INFO("control-c detected");

  ws.sync_close();
  return 0;
}


int main(int, char**) {

  Logger::instance().set_level(Logger::debug);
  Logger::instance().set_detail(true);

  std::function<bool()> on_event_exception = [](){
    LOG_INFO("got exception");
    return true;
  };

  RealtimeEventLoop event_loop(on_event_exception);
  IoLoop io_loop;
  SslContext ssl_context(SslConfig(true));

  int ec = run(event_loop, io_loop, ssl_context);

  io_loop.sync_stop();

  return ec;
}
