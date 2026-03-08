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

#include <apex/net/DecodeBuffer.hpp>
#include <apex/net/HttpParser.hpp>
#include <apex/net/common.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <random>

namespace apex
{

class HttpParser;
class TcpSocket;

/** Exception thrown during IO processing of a new connection. This is an
 * unrecoverable error that prevents the correct construction of a protocol
 * encoder/decoder object, and so prevents creationg of a session from a new
 * socket connection.  Will lead to connection drop without any attempt to send
 * a final message (although protocol level messages maybe sent before
 * disconnect). */
class handshake_error : public std::runtime_error
{
public:
  explicit handshake_error(const std::string& msg)
    : std::runtime_error(msg.c_str())
  {
  }
};


namespace protocol_constants
{
/* Keep default interval under 1 minute, which is a typical timeout period
   chosen by load balancers etc. */
static const int default_ping_interval_ms = 30000;

static const int default_pong_min_interval_ms = 1000;

static const int default_max_missed_pings = 2;
} // namespace protocol_constants

/* Base class for encoding & decoding of bytes on the wire. */
class protocol
{
public:
  struct options {

    std::chrono::milliseconds ping_interval; /* 0 for no heartbeats */

    /* minimum allowed interval between replies to ping */
    std::chrono::milliseconds pong_min_interval;

    /* Maximum number of missed pings.  A connection that reaches this mumber of
     * missed pings will be dropped.  A missed ping is one that is not answered
     * by a pong, nor by any other received data. It is expected that a peer
     * will reply to a ping with a pong message, or, will send application data
     * or other control frame instead of a pong. */
    int max_missed_pings;

    options()
      : ping_interval(protocol_constants::default_ping_interval_ms),
        pong_min_interval(protocol_constants::default_pong_min_interval_ms),
        max_missed_pings(protocol_constants::default_max_missed_pings)
    {
      if (ping_interval.count() == 0 && max_missed_pings != 0)
        throw std::runtime_error(
            "cannot have non-zero max_missed_pings with zero ping_interval");
    }
  };

  struct protocol_callbacks {
    std::function<void(std::chrono::milliseconds)> request_timer;
    std::function<void(std::chrono::milliseconds)> protocol_closed;
  };

  typedef std::function<void(const char*, size_t)> t_msg_cb;
  typedef std::function<void()> t_initiate_cb;

  protocol(TcpSocket*, t_msg_cb, protocol_callbacks,
           size_t buf_initial_size = 1, size_t buf_max_size = 65536);

  virtual ~protocol() = default;

protected:
  std::string fd() const;

  TcpSocket* _socket; /* non owning */
  t_msg_cb _on_data_cb;
  protocol_callbacks _callbacks;
  DecodeBuffer _buf;
};


class WebsocketProtocol : public protocol
{
public:
  struct options : public protocol::options {

    explicit options(std::string request_uri = "/")
      : host_header(host_header_mode::automatic),
        request_uri(std::move(request_uri))
    {
    }

    /* Construct from a base class instance */
    explicit options(const protocol::options& rhs)
      : protocol::options(rhs), host_header(host_header_mode::automatic)
    {
    }

    enum class host_header_mode { automatic = 0, custom, omit } host_header;
    std::string custom_host_header;

    /* Value of the Request-URI to use in HTTP GET request */
    std::string request_uri;

    /* Additional HTTP headers to place in the GET request */
    std::vector<std::pair<std::string, std::string>> extra_headers;
  };

  WebsocketProtocol(TcpSocket*,
                    t_msg_cb,
                    protocol::protocol_callbacks,
                    connect_mode _mode,
                    options);

  // start & stop
  void initiate(t_initiate_cb);
  bool initiate_close();

  // events into the protocol
  void on_read(char* src, size_t len);
  void on_timer();

  // send
  void send_msg(const char*, size_t);
  void send_ping();
  void send_pong(std::string_view sv = {});

  // state
  bool is_open() const { return _state == state::open; }
  bool is_opening() const { return _state == state::handling_http_request ||
      _state == state::handling_http_response; }

private:

  uint64_t process_frame_bytes(DecodeBuffer::read_pointer&);
  uint64_t process_http_request(DecodeBuffer::read_pointer&);
  uint64_t process_http_response(DecodeBuffer::read_pointer&);

  const std::string& header_field(const char*) const;

  void send_close(uint16_t, const std::string&);

  enum class state {
    invalid,
    handling_http_request,  // server
    handling_http_response, // client
    open,
    closing,
    closed,
  } _state = state::invalid;
  t_initiate_cb _on_open_cb;
  std::unique_ptr<HttpParser> _http_parser;
  options _options;
  std::string _expected_accept_key;
  std::chrono::time_point<std::chrono::steady_clock> _last_pong;
  std::atomic<int> _missed_pings;
  std::mt19937 _rng;
};

} // namespace apex
