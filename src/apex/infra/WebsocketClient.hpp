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

#include <apex/infra/SslSocket.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/infra/WebsocketProtocol.hpp>

#include <functional>
#include <iostream>
#include <memory>

namespace apex
{

class TcpSocket;
class UvErr;
class WebsocketProtocol;
class protocol;
class RealtimeEventLoop;

/*
 * Asynchronous websocket client
 */
class WebsocketClient : public std::enable_shared_from_this<WebsocketClient>
{

public:
  using OnOpenCallback = std::function<void()>;
  using OnErrorCallback = std::function<void()>;
  using OnCloseCallback = std::function<void()>;
  using MsgCallback = std::function<void(const char*, size_t)>;

  WebsocketClient(RealtimeEventLoop&,
                  std::unique_ptr<TcpSocket> sock,
                  std::string path,
                  MsgCallback msg_cb,
                  OnOpenCallback on_open,
                  OnCloseCallback on_close);

  ~WebsocketClient();

  void send(const char*, size_t);
  void send(const char*);

  bool is_open() const { return _is_open; }

  void sync_close();

private:
  void io_on_read(char* src, size_t len);

  void io_on_error(int ec);

  RealtimeEventLoop& _event_loop;
  std::unique_ptr<TcpSocket> _socket;
  WebsocketProtocol* _proto;
  std::unique_ptr<protocol> new_proto;
  std::string _path;
  OnCloseCallback _on_close;
  bool _is_open;
};

} // namespace apex
