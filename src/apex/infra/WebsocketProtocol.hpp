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

#include <apex/infra/DecodeBuffer.hpp>
#include <apex/infra/HttpParser.hpp>
#include <apex/util/utils.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <random>

namespace apex
{

class HttpParser;
class WebsocketppImpl;
struct websocketpp_msg;
class TcpSocket;


enum class serialiser_type { none = 0x00, json = 0x01 };

enum class protocol_type { none = 0x00, websocket = 0x01 };


constexpr inline int operator&(int lhs, serialiser_type rhs)
{
  return lhs & static_cast<int>(rhs);
}

constexpr inline int operator|(serialiser_type lhs, serialiser_type rhs)
{
  return (int)(static_cast<int>(lhs) | static_cast<int>(rhs));
}

constexpr inline int operator|(protocol_type lhs, protocol_type rhs)
{
  return (int)(static_cast<int>(lhs) | static_cast<int>(rhs));
}


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


/** Exception thrown due to any sort of malformed protocol message.  Generally
 * this error is thrown when the peer has failed to respect the terms of the
 * message-level protocol. Some examples: missinng mandatory arguments in
 * messages; values that have incorrect primitive type or container type (eg an
 * object where an array was expected).  This exception can be thrown during
 * initial message processing on the IO thread, or later during deferred
 * processing on the EV thread.  Shall result in a connection drop. */
class protocol_error : public std::runtime_error
{
public:
  explicit protocol_error(const std::string& msg)
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
    int serialisers; /* mask of enum serialiser_type bits */

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
      : serialisers((int)serialiser_type::json),
        ping_interval(protocol_constants::default_ping_interval_ms),
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

  protocol(TcpSocket*, t_msg_cb, protocol_callbacks, connect_mode m,
           size_t buf_initial_size = 1, size_t buf_max_size = 1024);

  virtual ~protocol() = default;

  /* Initiate the protocol closure handshake.  Returns false if the protocol
   * state doesn't support closure handshake or if already closed, so allowing
   * the caller to proceed with wamp session closure.  Otherwise returns true,
   * to indicate that closure handshake will commence and will be completed
   * later.  */
  virtual bool initiate_close() = 0;

  virtual void on_timer() {}

  virtual void io_on_read(char*, size_t) = 0;

  virtual void initiate(t_initiate_cb) = 0;

  virtual const char* name() const = 0;

  virtual void send_msg(const char*, size_t) = 0;

  [[nodiscard]] connect_mode mode() const { return m_mode; }

protected:
  std::string fd() const;

  TcpSocket* m_socket; /* non owning */
  t_msg_cb m_msg_processor;
  protocol_callbacks m_callbacks;
  DecodeBuffer m_buf;

private:
  connect_mode m_mode;
};


class WebsocketProtocol : public protocol
{
public:
  struct options : public protocol::options {

    /** Default serialiser for client initiated connection */
    static const int default_client_serialiser = (int)serialiser_type::json;

    explicit options(std::string __request_uri = "/")
      : host_header(host_header_mode::automatic),
        request_uri(std::move(__request_uri))
    {
    }

    /** Construct from a base class instance */
    explicit options(const protocol::options& rhs)
      : protocol::options(rhs), host_header(host_header_mode::automatic)
    {
    }

    enum class host_header_mode { automatic = 0, custom, omit } host_header;
    std::string custom_host_header;

    /** Value of the Request-URI to use in HTTP GET request */
    std::string request_uri;

    /** Additional HTTP headers to place in the GET request */
    std::vector<std::pair<std::string, std::string>> extra_headers;
  };

  static constexpr const char* NAME = "websocket";

  static constexpr const int HEADER_SIZE = 4; /* "GET " */
  static constexpr const char* MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

  static constexpr const char* WAMPV2_JSON_SUBPROTOCOL = "wamp.2.json";

  static constexpr const char* RFC6455 = "13";

  WebsocketProtocol(TcpSocket*, t_msg_cb, protocol::protocol_callbacks,
                    connect_mode _mode, options);

  bool initiate_close() override;
  void on_timer() override;
  void io_on_read(char*, size_t) override;
  void initiate(t_initiate_cb) override;

  [[nodiscard]] const char* name() const override { return NAME; }
  void send_msg(const char*, size_t) override;

private:
  void process_frame_bytes(DecodeBuffer::read_pointer&);

  const std::string& header_field(const char*) const;


  static const char* to_header(serialiser_type);
  static int to_opcode(serialiser_type);

  void send_ping();
  void send_pong(const std::string& payload = {});
  void send_close(uint16_t, const std::string&);
  void send_impl(const websocketpp_msg&);

  // TODO: add the mutex
  enum class state {
    invalid,
    handling_http_request,  // server
    handling_http_response, // client
    open,
    closing,
    closed,
  } _state = state::invalid;

  t_initiate_cb _initiate_cb;

  std::unique_ptr<HttpParser> _http_parser;

  options _options;

  std::string _expected_accept_key;

  std::unique_ptr<WebsocketppImpl> _websock_impl;

  std::chrono::time_point<std::chrono::steady_clock> _last_pong;

  std::atomic<int> _missed_pings;
};


} // namespace apex
