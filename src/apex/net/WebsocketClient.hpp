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

#include <apex/net/SslSocket.hpp>
#include <apex/net/TcpSocket.hpp>
#include <apex/net/WebsocketProtocol.hpp>

#include <functional>
#include <iostream>
#include <memory>

namespace apex
{

class TcpSocket;
class WebsocketProtocol;
class RealtimeEventLoop;
class WebsocketClient;

/* Utility function to synchronously establish a websocket */

std::shared_ptr<WebsocketClient> connect_websocket(
  const std::string& addr, // eg, "wss://cryptoexchange.com/api"
  std::string_view label,
  Reactor * reactor,
  SslContext* ssl,
  RealtimeEventLoop* timer_thread,
  std::function<void(const char* buf, size_t n)> on_message,
  SslSocket::Options = SslSocket::Options(),
  size_t recv_buf_len = 65536
  );

/*
 * Asynchronous websocket client
 */
class WebsocketClient : public std::enable_shared_from_this<WebsocketClient>
{

public:
  using OnOpenCallback = std::function<void()>;
  using OnErrorCallback = std::function<void()>;
  using OnCloseCallback = std::function<void()>;
  using OnDataCallback = std::function<void(const char*, size_t)>;

  WebsocketClient(RealtimeEventLoop&,
                  std::unique_ptr<TcpSocket> sock,
                  std::string path,
                  OnDataCallback on_data,
                  OnOpenCallback on_open,
                  OnCloseCallback on_close);

  ~WebsocketClient();

  void send(std::string_view);
  void send(const char*, size_t);
  void send(const char*);

  bool is_open() const { return _is_open; }

  void send_ping();
  void send_pong();

  int fd() const {
    return (_socket)? _socket->fd() : -1;
  }

  TimeLog& timelog() { return _socket->timelog(); }

private:

  RealtimeEventLoop& _event_loop;
  std::unique_ptr<TcpSocket> _socket;
  WebsocketProtocol* _proto{};
  std::string _path;
  OnCloseCallback _on_close;
  bool _is_open;
};

} // namespace apex
