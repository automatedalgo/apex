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

#include <apex/infra/WebsocketClient.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/core/Logger.hpp>

namespace apex
{


WebsocketClient::WebsocketClient(RealtimeEventLoop& evloop,
                                 std::unique_ptr<TcpSocket> sock,
                                 std::string path, MsgCallback msg_cb,
                                 OnOpenCallback on_open,
                                 OnCloseCallback on_close)
  : _event_loop(evloop),
    _socket(std::move(sock)),
    _path(path),
    _on_close(std::move(on_close)),
    _is_open(false)
{
  auto request_timer_cb = [this](std::chrono::milliseconds interval) {
    /* If protocol has requested a timer, register a reoccurring event to call
     * the protocol's on_timer function. Called during construction of
     * protocol. */
    if (interval.count() > 0) {
      auto timerfn = [wp{this->weak_from_this()},
                      interval]() -> std::chrono::milliseconds {
        if (auto sp = wp.lock()) {
          sp->_proto->on_timer();
          return interval;
        } else {
          /* shared_ptr invalid, so cancel timer */
          return std::chrono::milliseconds();
        }
      };
      this->_event_loop.dispatch(interval, std::move(timerfn));
    }
  };


  auto protocol_closed_fn = [this](std::chrono::milliseconds) {
    /* io-thread */
    this->_is_open = false;
    this->_on_close();
    std::cout << "protocol_closed_fn\n"; // TODO@WORK -- what was this in wamp?
  };


  // build the wire level protocol handler
  WebsocketProtocol::options protocol_options;
  protocol_options.request_uri = path;
  _proto = new WebsocketProtocol(
      this->_socket.get(), msg_cb,
      {std::move(request_timer_cb), std::move(protocol_closed_fn)},
      connect_mode::active, protocol_options);


  // start socket read
  this->_socket->start_read(
      [this](char* s, size_t n) { this->io_on_read(s, n); },
      [this](UvErr ec) { this->io_on_error(ec); });

  _proto->initiate([this, on_open]() {
    this->_is_open = true;
    if (on_open) {
      on_open();
    }
  });
}

WebsocketClient::~WebsocketClient() {
  if (!_socket->is_closed()) {
    // request socket close on the event loop
    if (_event_loop.this_thread_is_ev()) {
      _socket->close().wait();
    }
  }
}

void WebsocketClient::io_on_read(char* src, size_t len)
{
  /* io-thread */
  if (len > 0) {
    if (new_proto)
      new_proto->io_on_read(src, len);
    else
      _proto->io_on_read(src, len);
  }
}

void WebsocketClient::io_on_error(UvErr ec)
{
  /* io-thread */
  _is_open = false;
  LOG_WARN("lost websocket connection, error " << ec);
  if (_on_close)
    _on_close();
}


void WebsocketClient::send(const char* buf, size_t len)
{
  if (new_proto)
    new_proto->send_msg(buf, len);
  else if (_proto)
    _proto->send_msg(buf, len);
  else
    throw std::runtime_error(
        "cannot send on websocket before protocol established");
}

void WebsocketClient::send(const char* buf)
{
  this->send(buf, strlen(buf));
}


void WebsocketClient::sync_close() {
  auto fut = _socket->close();
  fut.wait();
}

} // namespace apex
