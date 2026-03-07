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

#include <apex/net/WebsocketClient.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/core/Logger.hpp>

#include <cassert>

namespace apex
{

/* Utility function to synchronously establish a websocket */
std::shared_ptr<WebsocketClient> connect_websocket(
  const std::string& addr,
  std::string_view label,
  Reactor * reactor,
  SslContext* ssl,
  RealtimeEventLoop* timer_thread,
  std::function<void(const char* buf, size_t n)> on_data,
  SslSocket::Options options,
  size_t recv_buf_len
  )
{
  auto url_parts = parse_websocket_url(addr);
  if (!url_parts)
    throw std::runtime_error(concat("bad websocket url: '",addr,"'"));

  std::string host = url_parts.host;
  int port = url_parts.port? std::stoi(*url_parts.port) : 443;
  auto path = concat(url_parts.path,
                     (url_parts.query)? "?":"",
                     (url_parts.query)? url_parts.query.value():"");

  int timeout_secs = 5;

  LOG_INFO(label << ": attempting websocket connection to '" << host << ":"
           << port << path << "'");

  /* ----- socket connection ----- */

  auto conn_promise = std::make_shared<std::promise<int>>();
  TcpSocket::connect_complete_cb_t connected_cb = [&](int err) {
    conn_promise->set_value(err);
  };

  std::unique_ptr<TcpSocket> sock;
  if (url_parts.is_ws()) {
    sock = std::make_unique<TcpSocket>(reactor);
  }
  else if (url_parts.is_wss()) {
    SslSocket::Options ssl_options;
    options.sni_policy =  SslSocket::Options::use_addr;
    sock = std::make_unique<SslSocket>(ssl, reactor, ssl_options);
  }
  sock->set_recv_buf_len(recv_buf_len);

  sock->connect(host, port, timeout_secs, connected_cb);

  auto conn_fut = conn_promise->get_future();

  if (conn_fut.wait_for(std::chrono::seconds(timeout_secs)) != std::future_status::ready)
    throw std::runtime_error("timeout during connect");

  int conn_err = conn_fut.get();
  if (conn_err)
    throw std::runtime_error("connect failed");

  /* ----- websocket initialisation ----- */

  auto completion_promise = std::make_shared<std::promise<void>>();
  auto on_open = [&] {
    completion_promise->set_value();
  };

  std::function<void()> on_down = [](){};

  std::shared_ptr<WebsocketClient> ws = std::make_shared<WebsocketClient>(
    *timer_thread, std::move(sock), url_parts.path, on_data, on_open, on_down);

  {
    // wait for the websocket to become open
    auto fut = completion_promise ->get_future();
    if (fut.wait_for(std::chrono::seconds(timeout_secs)) != std::future_status::ready)
      throw std::runtime_error("timeout during websocket initiation");
  }

  LOG_INFO(label << ": websocket established, fd: " << ws->fd(););
  return ws;
}


WebsocketClient::WebsocketClient(RealtimeEventLoop& evloop,
                                 std::unique_ptr<TcpSocket> sock,
                                 std::string path,
                                 OnDataCallback on_data,
                                 OnOpenCallback on_open,
                                 OnCloseCallback on_close)
  : _event_loop(evloop),
    _socket(std::move(sock)),
    _path(path),
    _on_close(std::move(on_close)),
    _is_open(false)
{
  assert(_on_close);
  assert(on_open);

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
    LOG_WARN("protocol_closed_fn");
  };

  // build the wire level protocol handler
  WebsocketProtocol::options protocol_options;
  protocol_options.request_uri = path;

  _proto = new WebsocketProtocol(
    this->_socket.get(),
    std::move(on_data),
    { std::move(request_timer_cb),
      std::move(protocol_closed_fn)
    },
    connect_mode::connect,
    protocol_options
    );

  // start socket read
  this->_socket->start_read([this](char* s, ssize_t n) {
    if (n > 0) {
      _proto->on_read(s, (size_t)n);
    }
    else {
      _is_open = false;
      if (n == 0) {
        LOG_INFO("websocket closed by peer");
      } else {
        LOG_WARN("lost websocket connection, error " << -n);
      }
      if (_on_close)
        _on_close();
    }
  });

  _proto->initiate([this, on_open]() {
    this->_is_open = true;
    if (on_open) {
      on_open();
    }
  });
}

WebsocketClient::~WebsocketClient() {
  delete _proto;
}


void WebsocketClient::send(std::string_view sv)
{
  this->send(sv.data(), sv.size());
}


void WebsocketClient::send(const char* buf, size_t len)
{
  _proto->send_msg(buf, len);
}


void WebsocketClient::send(const char* buf)
{
  this->send(buf, strlen(buf));
}


void WebsocketClient::send_ping() {
  _proto->send_ping();
}


void WebsocketClient::send_pong() {
  _proto->send_pong();
}

} // namespace apex
