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

#include <apex/infra/WebsocketProtocol.hpp>
#include <apex/infra/HttpParser.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/infra/WebsocketppImpl.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/platform.hpp>
#include <apex/util/utils.hpp>

#include <apache/base64.h> // from 3rdparty

#include <assert.h>
#include <string.h>

#include <openssl/sha.h>

#define HTML_BODY                                                              \
  "<!DOCTYPE html><html lang=\"en\"><head><meta "                              \
  "charset=\"UTF-8\"></head><body></body></html>"
#define HTML_BODY_LEN 86

// Would be simpler to use strlen, but on Visual Studio strlen is not constexpr.
static constexpr char html_body[] = HTML_BODY;
static constexpr int html_body_len = sizeof(html_body) - 1;
static_assert(html_body_len == HTML_BODY_LEN, "length check");

static const std::string http_200_response =
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n"
    "Content-Length: " STRINGIFY(HTML_BODY_LEN) "\r\n\r\n" HTML_BODY;

namespace apex
{

protocol::protocol(TcpSocket* h, t_msg_cb cb, protocol_callbacks callbacks,
                   connect_mode _mode, size_t buf_initial_size,
                   size_t buf_max_size)
  : m_socket(h),
    m_msg_processor(cb),
    m_callbacks(callbacks),
    m_buf(buf_initial_size, buf_max_size),
    m_mode(_mode)
{
}


std::string protocol::fd() const { return std::to_string(m_socket->fd()); }


/**
 * This struct is used to hide the websocketpp types from public headers.
 */
struct websocketpp_msg {
  WebsocketConfig::message_type::ptr ptr;
};


WebsocketProtocol::WebsocketProtocol(TcpSocket* h, t_msg_cb msg_cb,
                                     protocol::protocol_callbacks callbacks,
                                     connect_mode mode, options opts)
  : protocol(h, msg_cb, callbacks, mode),
    _state(mode == connect_mode::accept ? state::handling_http_request
           : state::handling_http_response),
    _http_parser(new HttpParser(mode == connect_mode::accept
                                     ? HttpParser::e_http_request
                                     : HttpParser::e_http_response)),
    _options(std::move(opts)),
    _websock_impl(new WebsocketppImpl(mode)),
    _last_pong(std::chrono::steady_clock::now()),
    _missed_pings(0)
{
  // register to receive heartbeat callbacks
  if (_options.ping_interval.count() > 0)
    callbacks.request_timer(_options.ping_interval);
}


inline std::string make_accept_key(const std::string& challenge)
{
  auto full_key = challenge + WebsocketProtocol::MAGIC;
  unsigned char obuf[20];

  SHA1((const unsigned char*)full_key.c_str(), full_key.size(), obuf);

  char tmp[50] = {0};
  assert(ap_base64encode_len(sizeof(obuf)) < (int)sizeof(tmp));
  assert(tmp[sizeof(tmp) - 1] == 0);

  ap_base64encode(tmp, (char*)obuf, sizeof(obuf));

  return tmp;
}


/* Test whether a HTTP header contains a desired value.  Note that when checking
 * request and response headers, we are generally case
 * insensitive. I.e. according to RFC2616, all header field names in both HTTP
 * requests and HTTP responses are case-insensitive. */
static bool header_contains(const std::string& source, const std::string& match)
{
  for (auto& i : split(source, ',')) {
    std::string trimmed = trim(i);
    if (strcasecmp(trimmed.c_str(), match.c_str()) == 0)
      return true;
  }
  return false;
}


void WebsocketProtocol::send_msg(const char* buf, size_t len)
{
  websocketpp::frame::opcode::value op{};
  op = websocketpp::frame::opcode::text;

  auto msg_ptr = _websock_impl->msg_manager()->get_message(op, len);
  msg_ptr->append_payload(buf, len);
  auto out_msg_ptr = _websock_impl->msg_manager()->get_message();

  if (out_msg_ptr == nullptr)
    throw std::runtime_error("failed to obtain msg object");

  auto ec =
      _websock_impl->processor()->prepare_data_frame(msg_ptr, out_msg_ptr);
  if (ec)
    throw std::runtime_error(ec.message());

  LOG_DEBUG("fd: " << fd() << ", frame_tx: "
                   << WebsocketppImpl::frame_to_string(out_msg_ptr));

  m_socket->write(out_msg_ptr->get_header().data(), out_msg_ptr->get_header().size());
  m_socket->write(out_msg_ptr->get_payload().data(), out_msg_ptr->get_payload().size());
}


const std::string& WebsocketProtocol::header_field(const char* field) const
{
  if (!_http_parser->has(field)) {
    std::string msg = "http header missing ";
    msg += field;
    throw handshake_error(msg);
  } else
    return _http_parser->get(field);
}


void WebsocketProtocol::io_on_read(char* src, size_t len)
{
  /* IO thread */

  while (len) {
    size_t consume_len = m_buf.consume(src, len);
    src += consume_len;
    len -= consume_len;

    auto rd = m_buf.read_ptr();
    while (rd.avail()) {
      if (_state == state::handling_http_request) {
        auto consumed = _http_parser->handle_input(rd.ptr(), rd.avail());
        LOG_DEBUG("fd: " << fd()
                         << ", http_rx: " << std::string(rd.ptr(), consumed));
        rd.advance(consumed);

        if (_http_parser->is_good() == false)
          throw handshake_error("bad http header: " +
                                _http_parser->error_text());

        if (_http_parser->is_complete()) {
          if (_http_parser->is_upgrade() && _http_parser->has("upgrade") &&
              header_contains(_http_parser->get("upgrade"), "websocket") &&
              _http_parser->has("sec-websocket-key") &&
              _http_parser->has("sec-websocket-version")) {
            auto& websock_key = header_field("sec-websocket-key");
            auto& websock_ver = header_field("sec-websocket-version");

            if (websock_ver != RFC6455 /* 13 */)
              throw handshake_error("incorrect websocket version");

            bool sec_websocket_protocol_present =
                _http_parser->has("sec-websocket-protocol");
            if (sec_websocket_protocol_present) {
              // auto& websock_sub = header_field("sec-websocket-protocol");

              /* Note, here we would identify common protocol to use, but
               * binance has no options other that json */
            }

            std::ostringstream os;
            os << "HTTP/1.1 101 Switching Protocols\r\n"
               << "Upgrade: websocket\r\n"
               << "Connection: Upgrade\r\n"
               << "Sec-WebSocket-Accept: " << make_accept_key(websock_key)
               << "\r\n";
            os << "Sec-WebSocket-Protocol: json\r\n";
            // if (sec_websocket_protocol_present)
            //   os << "Sec-WebSocket-Protocol: " << to_header(m_codec->type())
            //   << "\r\n";
            os << "\r\n";
            std::string msg = os.str();

            LOG_DEBUG("fd: " << fd() << ", http_tx: " << msg);

            m_socket->write(msg.c_str(), msg.size());
            _state = state::open;
          } else if (_http_parser->has("connection") &&
                     header_contains(_http_parser->get("connection"),
                                     "close")) {
            /* Received a http header that requests connection close.  This is
             * straight-forward to obey (just echo the header and close the
             * socket). This kind of request can be received when connected to a
             * load balancer that is checking server health. */

            LOG_DEBUG("fd: " << fd() << ", http_tx: " << http_200_response);
            m_socket->write(http_200_response.c_str(),
                            http_200_response.size());
            _state = state::closed;

            // request session closure after delay, gives time of peer to close,
            // and for message to be fully written
            m_callbacks.protocol_closed(std::chrono::milliseconds(3000));
          } else
            throw handshake_error("http header is not a websocket upgrade");
        }
      } else if (_state == state::handling_http_response) {
        auto consumed = _http_parser->handle_input(rd.ptr(), rd.avail());
        LOG_DEBUG("fd: " << fd()
                         << ", http_rx: " << std::string(rd.ptr(), consumed));
        rd.advance(consumed);

        if (_http_parser->is_good() == false)
          throw handshake_error("bad http header: " +
                                _http_parser->error_text());

        if (_http_parser->is_complete()) {
          if (_http_parser->is_upgrade() && _http_parser->has("upgrade") &&
              header_contains(_http_parser->get("upgrade"), "websocket") &&
              _http_parser->has("sec-websocket-accept") &&
              _http_parser->http_status_phrase() == "Switching Protocols" &&
              _http_parser->http_status_code() ==
                  HttpParser::status_code_switching_protocols) {
            auto& websock_key = header_field("sec-websocket-accept");
            // auto& websock_sub = header_field("sec-websocket-protocol");

            if (websock_key != _expected_accept_key)
              throw handshake_error("incorrect key for Sec-WebSocket-Accept");


            _state = state::open;
            _initiate_cb();
          } else
            throw handshake_error("http header is not a websocket upgrade");
        }
      } else {
        /* for all other websocket states, use the websocketpp parser */
        process_frame_bytes(rd);
      }
    }

    m_buf.discard(rd); /* shift unused bytes to front of DecodeBuffer */
  }
}


void WebsocketProtocol::initiate(t_initiate_cb cb)
{
  _initiate_cb = cb;

  char nonce[16];
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_int_distribution<> distr(0x00, 0xFF);
  for (auto& x : nonce)
    x = distr(engine);

  char sec_websocket_key[30] = {0};
  assert(sec_websocket_key[sizeof(sec_websocket_key) - 1] == 0);
  assert(ap_base64encode_len(sizeof(nonce)) < (int)sizeof(sec_websocket_key));

  ap_base64encode(sec_websocket_key, nonce, sizeof(nonce));

  std::ostringstream oss;
  oss << "GET " << _options.request_uri
      << " HTTP/1.1\r\n"
         "Pragma: no-cache\r\n"
         "Cache-Control: no-cache\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n";
  switch (_options.host_header) {
    case options::host_header_mode::automatic: {
      oss << "Host: " << m_socket->node() << ":" << m_socket->service()
          << "\r\n";
      /*
      oss << "Host: "
          << "api.huobi.pro"
          << ":"
          << "443"
          << "\r\n";
          */
      break;
    }
    case options::host_header_mode::custom: {
      oss << "Host: " << _options.custom_host_header << "\r\n";
      break;
    }
    case options::host_header_mode::omit: /* nothing to add to header */
      break;
  }

  oss << "Sec-WebSocket-Key: " << sec_websocket_key
      << "\r\n"
         "Sec-WebSocket-Protocol: json"
      << "\r\n";

  oss << "Sec-WebSocket-Version: " << RFC6455 << "\r\n";

  for (auto& item : _options.extra_headers)
    oss << item.first << ": " << item.second << "\r\n";

  oss << "\r\n";
  std::string http_request = oss.str();

  _expected_accept_key = make_accept_key(sec_websocket_key);

  LOG_DEBUG("fd: " << fd() << ", http_tx: " << http_request);
  m_socket->write(http_request.c_str(), http_request.size());
}


void WebsocketProtocol::send_impl(const websocketpp_msg& msg)
{
  LOG_DEBUG("fd: " << fd() << ", frame_tx: "
                   << WebsocketppImpl::frame_to_string(msg.ptr));

  if (msg.ptr->get_payload().empty()) {
    m_socket->write(msg.ptr->get_header().data(), msg.ptr->get_header().size());
  } else {

    m_socket->write(msg.ptr->get_header().data(), msg.ptr->get_header().size());
    m_socket->write(msg.ptr->get_payload().data(), msg.ptr->get_payload().size());
  }
}


void WebsocketProtocol::send_ping()
{
  websocketpp_msg msg{_websock_impl->msg_manager()->get_message()};
  _websock_impl->processor()->prepare_ping("", msg.ptr);
  send_impl(msg);
}


void WebsocketProtocol::send_pong(const std::string& payload)
{
  websocketpp_msg msg{_websock_impl->msg_manager()->get_message()};
  _websock_impl->processor()->prepare_pong(payload, msg.ptr);
  send_impl(msg);
}


void WebsocketProtocol::send_close(uint16_t code, const std::string& reason)
{
  websocketpp_msg msg{_websock_impl->msg_manager()->get_message()};
  _websock_impl->processor()->prepare_close(code, reason, msg.ptr);
  send_impl(msg);
}


void WebsocketProtocol::on_timer()
{
  /* EV thread */
  if (_state == state::open) {
    if (_missed_pings.load() >= _options.max_missed_pings) {
      send_close(websocketpp::close::status::protocol_error, "");
      _state = state::closed;
      m_callbacks.protocol_closed(std::chrono::milliseconds(0));
    } else {
      /* assume our next ping will be missed; the count will be reset on arrival
       * of data from peer */
      ++_missed_pings;
      send_ping();
    }
  }
}


const char* WebsocketProtocol::to_header(serialiser_type p)
{
  switch (p) {
    case serialiser_type::none:
      return "";
    case serialiser_type::json:
      return WAMPV2_JSON_SUBPROTOCOL;
  }
  return "";
}


void WebsocketProtocol::process_frame_bytes(DecodeBuffer::read_pointer& rd)
{
  /* Feed bytes into the websocketpp stream parser. The parser will take only
   * the bytes required to build the next websocket message; it won't slurp all
   * the available bytes (i.e. its possible the consumed-count can be non-zero
   * after the consume() operation). */
  websocketpp::lib::error_code ec;
  size_t consumed =
      _websock_impl->processor()->consume((uint8_t*)rd.ptr(), rd.avail(), ec);
  rd.advance(consumed);

  if (ec)
    throw std::runtime_error(ec.message());

  if (_websock_impl->processor()->get_error())
    throw std::runtime_error("websocket parser fatal error");

  // treat arrival of any data as reseting the missed pings counter
  _missed_pings.store(0);

  if (_websock_impl->processor()->ready()) {
    // shared_ptr<message_buffer::message<...> >
    auto msg = _websock_impl->processor()->get_message();

    if (!msg)
      throw std::runtime_error("none message from websocketpp");

    LOG_DEBUG("fd: " << fd() << ", frame_rx: "
                     << WebsocketppImpl::frame_to_string(msg));

    // if (msg->get_payload().size())
    //   LOG_INFO(std::string(msg->get_payload().data(),
    //   msg->get_payload().size()));

    if (_state == state::closed)
      return; // ingore bytes after protocol closed

    if (!is_control(msg->get_opcode())) {
      // data message, dispatch to user
      if ((msg->get_opcode() == websocketpp::frame::opcode::binary) ||
          (msg->get_opcode() == websocketpp::frame::opcode::text)) {


        // TODO: is user throws, what should
        /* user callback of raw data */
        m_msg_processor(msg->get_payload().data(), msg->get_payload().size());
      }
    } else {
      // control message
      websocketpp::frame::opcode::value op = msg->get_opcode();

      if (op == websocketpp::frame::opcode::PING) {
        const auto now = std::chrono::steady_clock::now();
        if ((now > _last_pong) &&
            (now - _last_pong >= _options.pong_min_interval)) {
          _last_pong = now;
          send_pong(msg->get_payload());
          return;
        }
      } else if (op == websocketpp::frame::opcode::PONG) {
        // no-op
      } else if (op == websocketpp::frame::opcode::CLOSE) {
        if (_state == state::closing) {
          // sent & received close-frame, so protocol closed
          _state = state::closed;
          m_callbacks.protocol_closed(std::chrono::milliseconds(0));
        } else if (_state == state::open) {
          // received & sending close-frame, so protocol closed
          send_close(websocketpp::close::status::normal, "");
          _state = state::closed;

          // extract close code & message -- optional
          //
          //  first 2 bytes, are an error code
          //  remainer might be an error string
          //
          //


          // LOG_WARN("got a close frame");
          // if ( msg->get_payload().size() > 0 ){
          //   if ( msg->get_payload().size() > 2) {
          //     msg->get_payload().size()
          //     return ntohs(addrin->sin_port);
          //   }
          // }

          send_pong(msg->get_payload());
          m_callbacks.protocol_closed(std::chrono::milliseconds(0));
        }
      }
    }
  }
}


bool WebsocketProtocol::initiate_close()
{
  /* Start the graceful close sequence. */
  _state = state::closing;
  send_close(websocketpp::close::status::normal, "");
  return true;
}

} // namespace apex
