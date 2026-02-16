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

namespace apex {

/* WebSocket protocol constants and low level functions */
namespace ws {

constexpr std::string_view RFC6455_version = "13";
constexpr std::string_view GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

namespace flag {

constexpr int fin = 0x80;
constexpr int mask = 0x80;
constexpr uint8_t opcode = 0x0F;

};

namespace close_status {

// normal closure, meaning that the purpose forwhich the connection was
// established has been fulfilled.
constexpr int normal = 1000;

// indicates that an endpoint is terminating the connection due
constexpr int protocol_error = 1002;

}

constexpr int basic_header_len = 2;
constexpr int payload_size_16 = 126;
constexpr int payload_size_64 = 127;

enum class opcode  {
  continuation = 0x0,
  text         = 0x1,
  binary       = 0x2,
  close        = 0x8,
  ping         = 0x9,
  pong         = 0xa
};


// constants related to frame and payload limits
namespace limits {

// maximum size of a basic WebSocket payload
constexpr uint8_t payload_size_basic = 125;

// maximum size of an extended WebSocket payload (basic payload = 126)
constexpr uint16_t payload_size_extended = 0xFFFF; // 2^16, 65535
}

/// The constant size component of a WebSocket frame header
struct basic_header {
  basic_header(ws::opcode op, uint64_t size, bool fin, bool mask)
    : bytes{}
  {
    bytes[0] |= fin? ws::flag::fin:0;
    bytes[0] |= (static_cast<uint8_t>(op) & ws::flag::opcode);
    bytes[1] |= mask? ws::flag::mask:0;

    uint8_t basic_value;

    if (size <= limits::payload_size_basic) {
      basic_value = static_cast<uint8_t>(size);
    } else if (size <= limits::payload_size_extended) {
      basic_value = payload_size_16;
    } else {
      basic_value = payload_size_64;
    }

    bytes[1] |= basic_value;
  }

  size_t header_size() const { return basic_header_len; }

  char bytes[basic_header_len];
};


inline size_t calc_header_len(uint8_t b1) {
  return 2 // basic header
    + ((b1 & 0x7F) == 126 ? 2 : (b1 & 0x7F) == 127 ? 8 : 0) // ext. header
    + ((b1 & ws::flag::mask) ? 4 : 0);
}


inline uint64_t calc_payload_len(const uint8_t * h) {
  uint64_t len = h[1] & 0x7F;
  if (len == 126) {
    len = (static_cast<uint64_t>(h[2]) << 8) | h[3];
  }
  else if (len == 127) {
    len = 0;
    for (int i = 0; i < 8; ++i)
      len = (len << 8) | h[2 + i];
  }
  return len;
}

inline ws::opcode calc_opcode(uint8_t h0) {
  return static_cast<ws::opcode>(h0 & ws::flag::opcode);
}


inline bool is_fin(uint8_t h0) {
  return (h0 & ws::flag::fin) == ws::flag::fin;
}

} // namespace ws

protocol::protocol(TcpSocket* h, t_msg_cb cb, protocol_callbacks callbacks,
                   size_t buf_initial_size,
                   size_t buf_max_size)
  : _socket(h),
    _on_data_cb(cb),
    _callbacks(std::move(callbacks)),
    _buf(buf_initial_size, buf_max_size)
{
  assert(_buf.pad_size() >= 64); // downstream JSON parser needs extra 64 bytes
}


std::string protocol::fd() const { return std::to_string(_socket->fd()); }


WebsocketProtocol::WebsocketProtocol(TcpSocket* h, t_msg_cb msg_cb,
                                     protocol::protocol_callbacks callbacks,
                                     connect_mode mode, options opts)
  : protocol(h, msg_cb, std::move(callbacks)),
    _state(mode == connect_mode::accept ? state::handling_http_request
           : state::handling_http_response),
    _http_parser(new HttpParser(mode == connect_mode::accept
                                ? HttpParser::e_http_request
                                : HttpParser::e_http_response)),
    _options(std::move(opts)),
    _last_pong{},
    _missed_pings(0),
    _rng{std::random_device{}()}
{
  // register to receive heartbeat callbacks
  if (_options.ping_interval.count() > 0)
    _callbacks.request_timer(_options.ping_interval);
}


inline std::string make_accept_key(const std::string& challenge)
{
  auto full_key = challenge;
  full_key += ws::GUID;

  unsigned char obuf[20] = {};

  SHA1((const unsigned char*)full_key.c_str(), full_key.size(), obuf);

  char tmp[50] = {};
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


const std::string& WebsocketProtocol::header_field(const char* field) const
{
  if (!_http_parser->has(field)) {
    std::string msg = "http header missing ";
    msg += field;
    throw handshake_error(msg);
  } else
    return _http_parser->get(field);
}


uint64_t WebsocketProtocol::process_http_request(DecodeBuffer::read_pointer& rd)
{
  uint64_t consumed = _http_parser->handle_input(rd.ptr(), rd.avail());
  LOG_DEBUG("fd: " << fd()
           << ", http_rx: " << std::string(rd.ptr(), consumed));

  if (_http_parser->is_good() == false)
    throw handshake_error("bad http header: " +
                          _http_parser->error_text());

  if (_http_parser->is_complete()) {
    if (_http_parser->is_upgrade() &&
        _http_parser->has("upgrade") &&
        header_contains(_http_parser->get("upgrade"), "websocket") &&
        _http_parser->has("sec-websocket-key") &&
        _http_parser->has("sec-websocket-version")) {
      auto& websock_key = header_field("sec-websocket-key");
      auto& websock_ver = header_field("sec-websocket-version");

      if (websock_ver != ws::RFC6455_version)
        throw handshake_error("unsupported websocket version");

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
      os << "\r\n";
      std::string msg = os.str();

      LOG_DEBUG("fd: " << fd() << ", http_tx: " << msg);

      _socket->write(msg.c_str(), msg.size());
      _state = state::open;
    } else if (_http_parser->has("connection") &&
               header_contains(_http_parser->get("connection"), "close")) {
      /* Received a http header that requests connection close.  This is
       * straight-forward to obey (just echo the header and close the
       * socket). This kind of request can be received when connected to a
       * load balancer that is checking server health. */

      LOG_DEBUG("fd: " << fd() << ", http_tx: " << http_200_response);
      _socket->write(http_200_response.c_str(),
                     http_200_response.size());
      _state = state::closed;

      // request session closure after delay, gives time of peer to close,
      // and for message to be fully written
      _callbacks.protocol_closed(std::chrono::milliseconds(3000));
    } else
      throw handshake_error("http header is not a websocket upgrade");
  }
  return consumed;
};


uint64_t WebsocketProtocol::process_http_response(DecodeBuffer::read_pointer& rd)
{
  uint64_t consumed = _http_parser->handle_input(rd.ptr(), rd.avail());
  LOG_DEBUG("fd: " << fd()
            << ", http_rx: " << std::string(rd.ptr(), consumed));

  if (_http_parser->is_good() == false)
    throw handshake_error("bad http header: " + _http_parser->error_text());

  if (_http_parser->is_complete()) {
    if (_http_parser->is_upgrade() &&
        _http_parser->has("upgrade") &&
        header_contains(_http_parser->get("upgrade"), "websocket") &&
        _http_parser->has("sec-websocket-accept") &&
        _http_parser->http_status_phrase() == "Switching Protocols" &&
        _http_parser->http_status_code() ==  HttpParser::status_code_switching_protocols) {
      auto& websock_key = header_field("sec-websocket-accept");

      if (websock_key != _expected_accept_key)
        throw handshake_error("incorrect key for Sec-WebSocket-Accept");

      _state = state::open;
      _on_open_cb();
    } else
      throw handshake_error("http header is not a websocket upgrade");
  }

  return consumed;
}


void WebsocketProtocol::on_read(char* src, size_t len)
{
  /* IO thread */

  while (len) {
    size_t copy_len = _buf.consume(src, len);
    src += copy_len;
    len -= copy_len;
    auto rd = _buf.read_ptr();
    while (rd.avail()) {
      uint64_t consumed = 0;
      switch (_state) {
        case state::open :
        case state::closing : {
          consumed = process_frame_bytes(rd);
          break;
        };
        case state::handling_http_request : {
          consumed = process_http_request(rd);
          break;
        }
        case state::handling_http_response : {
          consumed = process_http_response(rd);
          break;
        }
        case state::invalid :
        case state::closed :
          return;
      }
      rd.advance(consumed);
      if (consumed == 0)
        break;
    }
    _buf.discard(rd); // move unread bytes to front of buffer
  }
}


void WebsocketProtocol::initiate(t_initiate_cb cb)
{
  _on_open_cb = cb;

  char nonce[16];
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_int_distribution<> distr(0x00, 0xFF);
  for (auto& x : nonce)
    x = distr(engine);

  char sec_websocket_key[30] = {};
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
      oss << "Host: " << _socket->node() << ":" << _socket->service() << "\r\n";
      break;
    }
    case options::host_header_mode::custom: {
      oss << "Host: " << _options.custom_host_header << "\r\n";
      break;
    }
    case options::host_header_mode::omit:
      break;
  }

  oss << "Sec-WebSocket-Key: " << sec_websocket_key  << "\r\n"
      << "Sec-WebSocket-Protocol: json" << "\r\n"
      << "Sec-WebSocket-Version: " << ws::RFC6455_version << "\r\n";

  for (auto& item : _options.extra_headers)
    oss << item.first << ": " << item.second << "\r\n";

  oss << "\r\n";
  std::string http_request = oss.str();

  _expected_accept_key = make_accept_key(sec_websocket_key);

  LOG_DEBUG("fd: " << fd() << ", http_tx: " << http_request);
  _socket->write(http_request.c_str(), http_request.size());
}


void WebsocketProtocol::send_msg(const char* buf, size_t payload_len)
{
  char frame[2+4+8+65536]; // head(2) + mask(4) + ext(8) + payload(64KB)

  if (payload_len <= 0xFFFF) {
    const size_t hdr_len = 2 + 4 + ((payload_len<126)?0:2); // head(2) + mask(4) + ext(2)?
    const size_t frame_len = hdr_len + payload_len;

    frame[0] = ws::flag::fin | static_cast<int>(ws::opcode::text);

    int offset;
    if (payload_len < 126) {
      frame[1] = ws::flag::mask | static_cast<uint8_t>(payload_len);
      offset = 2;
    } else  {
      frame[1] = ws::flag::mask | 126;
      frame[2] = (payload_len >> 8) & 0xFF;
      frame[3] = payload_len & 0xFF;
      offset = 4;
    }

    // generate mask and copy into frame, after header
    uint32_t mask = _rng();
    uint8_t mask_bytes[4] = {
      static_cast<uint8_t>((mask >> 24) & 0xFF),
      static_cast<uint8_t>((mask >> 16) & 0xFF),
      static_cast<uint8_t>((mask >> 8) & 0xFF),
      static_cast<uint8_t>(mask & 0xFF)
    };
    frame[offset++] = mask_bytes[0];
    frame[offset++] = mask_bytes[1];
    frame[offset++] = mask_bytes[2];
    frame[offset++] = mask_bytes[3];

    // copy payload
    std::memcpy(&frame[offset], buf, payload_len);

    // apply mask
    for (size_t i = 0; i < payload_len; i++)
      frame[offset + i] = buf[i] ^ mask_bytes[i & 3];

    LOG_DEBUG("sending: len=" << frame_len << ", frame: " << to_hex(frame, frame_len) );
    _socket->write((char*) frame, frame_len);
  }
  else {
    throw std::runtime_error("sending huge frames not supported");
  }
}


void WebsocketProtocol::send_ping()
{
  ws::basic_header hdr {
    ws::opcode::ping,
    0, // no payload
    true, // fin
    false // mask
  };
  _socket->write(hdr.bytes, hdr.header_size());
}


void WebsocketProtocol::send_pong(std::string_view payload)
{
  if (payload.size() > 125) return; // control frames max 125 bytes

  ws::basic_header hdr {
    ws::opcode::pong,
    payload.size(), // payload len
    true, // fin
    true // mask
  };

  uint8_t frame[2 + 4 + 125] = {}; // header(2) + mask(4) + payload.max(125)
  frame[0] = hdr.bytes[0];
  frame[1] = hdr.bytes[1];

  // generate mask and copy into frame, after header
  uint32_t mask = _rng();
  uint8_t mask_bytes[4] = {
    static_cast<uint8_t>((mask >> 24) & 0xFF),
    static_cast<uint8_t>((mask >> 16) & 0xFF),
    static_cast<uint8_t>((mask >> 8) & 0xFF),
    static_cast<uint8_t>(mask & 0xFF)
  };
  frame[2] = mask_bytes[0];
  frame[3] = mask_bytes[1];
  frame[4] = mask_bytes[2];
  frame[5] = mask_bytes[3];

  // apply the mask
  for (size_t i = 0; i < payload.size(); i++)
    frame[2 + 4 + i] = payload[i] ^ mask_bytes[i & 3];

  _socket->write((char*) frame, 2 + 4 + payload.size());
}


void WebsocketProtocol::send_close(uint16_t /*code*/, const std::string& /*reason*/)
{
  uint8_t frame[2+4] = {}; // header(2) + mask (4)
  frame[0] = ws::flag::fin | static_cast<uint8_t>(ws::opcode::close);
  frame[1] = ws::flag::mask;
  _socket->write((char*) frame, std::size(frame));
}


void WebsocketProtocol::on_timer()
{
  /* EV thread */
  if (_state == state::open) {
    if (_missed_pings.load() >= _options.max_missed_pings) {
      send_close(apex::ws::close_status::protocol_error, "");
      _state = state::closed;
      _callbacks.protocol_closed(std::chrono::milliseconds(0));
    } else {
      /* assume our next ping will be missed; the count will be reset on arrival
       * of data from peer */
      ++_missed_pings;
      send_ping();
    }
  }
}


uint64_t WebsocketProtocol::process_frame_bytes(DecodeBuffer::read_pointer& rd)
{
  if (rd.avail() < ws::basic_header_len)
    return 0;

  const uint8_t * const hdr = reinterpret_cast<const uint8_t*>(rd.ptr());

  const uint64_t hdr_len = ws::calc_header_len(hdr[1]);

  if (rd.avail() < hdr_len)
    return 0;

  const uint64_t payload_len = ws::calc_payload_len(hdr);
  const uint64_t frame_len = hdr_len + payload_len;

  if (rd.avail() < frame_len)
    return 0;

  const auto op =  ws::calc_opcode(hdr[0]);
  const bool fin = ws::is_fin(hdr[0]);
  char * payload = rd.ptr() + hdr_len;

  _missed_pings.store(0); // any frame can reset missing pings counter

  if ((op == ws::opcode::binary) || (op == ws::opcode::text)) {
    _on_data_cb(payload, payload_len);
  } else if (op == ws::opcode::ping) {
    const auto now = std::chrono::steady_clock::now();
    if (((now > _last_pong) && (now - _last_pong >= _options.pong_min_interval)) ||
        (_last_pong == std::chrono::time_point<std::chrono::steady_clock>())
      ) {
      _last_pong = now;
      send_pong({payload, payload_len});
    }
  } else if (op == ws::opcode::pong) {
    // no-op
  } else if (op == ws::opcode::close) {
      LOG_DEBUG("received a websocket close frame");
      if (_state == state::closing) {
        // sent & received close-frame, so protocol closed
        _state = state::closed;
        _callbacks.protocol_closed(std::chrono::milliseconds(0));
      } else if (_state == state::open) {
        // received & sending close-frame, so protocol closed
        send_close(ws::close_status::normal, "");
        _state = state::closed;
        _callbacks.protocol_closed(std::chrono::milliseconds(0));
      }
  }

  if (!fin || op == ws::opcode::continuation) {
    LOG_ERROR("websocket fragemented frames not supported");
    initiate_close();
  }

  return frame_len;
}


bool WebsocketProtocol::initiate_close()
{
  /* Start the graceful close sequence. */
  _state = state::closing;
  send_close(ws::close_status::normal, "");
  return true;
}


} // namespace apex
