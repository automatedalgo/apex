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

#include <apex/comm/GxWireFormat.pb.h>
#include <apex/infra/DecodeBuffer.hpp>
#include <apex/util/RealtimeEventLoop.hpp>
#include <apex/infra/Reactor.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/core/Logger.hpp>

#include <memory>
#include <netinet/in.h>

namespace apex
{

class RealtimeEventLoop;
class TcpSocket;
class Reactor;

namespace gx
{

enum class Flags : uint8_t {
  proto3 = 1 << 0,
};


enum class Type : char {
  null = '0',
  subscribe = 'S',
  new_order = 'D',
  cancel_order = 'F',
  logon = 'A',
  trade = 't',
  subscribe_account = 's',
  account_update = 'u',
  tick_top = 'p',
  error = 'e',
  order_fill = 'f',
  om_logon = 'l',
  order_exec = 'x'
};

typedef uint32_t t_msgid;

struct Header {
  uint16_t len;   // full message length, ie, sizeof(header) + sizeof(payload)
  Type type;      // message type
  uint8_t flags;  // other message flags
  t_msgid id;     // message id, used for request & response matching
  char payload[]; // placeholder for payload data

  static void init(Header* msg, size_t payload_len, Type type, uint16_t flags)
  {
    memset(msg, 0, sizeof(Header));
    msg->len = payload_len + sizeof(Header);
    msg->type = type;
    msg->flags = flags;
  }

  void hton();
};
} // namespace gx


static_assert(sizeof(gx::Header) == 8);


template <typename T>
class GxSessionBase : public std::enable_shared_from_this<T>
{
public:
  struct EventHandlers {
    std::function<void(T&)> on_err;
  };


  GxSessionBase(Reactor* reactor,
                RealtimeEventLoop& evloop,
                std::unique_ptr<TcpSocket> sock)
    : _reactor(reactor),
      _event_loop(evloop),
      _sock(std::move(sock)),
      _buf(1000, 1000000)
  {
  }

  ~GxSessionBase()
  {
    // assert(this->_event_loop.this_)
    // if (_sock)
    //   _sock->close().wait();
  }

  void close() { _sock->close(); }

  TcpSocket* get_socket2() { return _sock.get(); }


  void start_read(std::function<void(T&, int)> err_cb)
  {
    auto wp = this->weak_from_this();

    on_read_cb_t on_read = [wp, err_cb](char* buf, ssize_t len){
      if (auto sp = wp.lock()) {
        if (len >= 0)
          sp->io_on_read(buf, len);
        else
        {
          sp->_event_loop.dispatch([=]() {
            if (auto sp = wp.lock()) {
              err_cb(*sp, -len);
            }
          });
        }
      }
    };

    _sock->start_read(on_read);
  }

protected:
  Reactor * _reactor;
  RealtimeEventLoop& _event_loop;
  std::unique_ptr<TcpSocket> _sock;


  /* Invoked on the io-thread */
  virtual void io_on_full_message(gx::Header* header, char* payload,
                                  size_t payload_len) = 0;

private:
  void io_on_read(char* src, size_t src_len)
  {
    /* io-thread */

    while (src_len) {
      const size_t consumed = _buf.consume(src, src_len);
      src += consumed;
      src_len -= consumed;

      // now attempt to parse whatever is in the DecodeBuffer
      auto rd = _buf.read_ptr();
      while (rd.avail() >= sizeof(apex::gx::Header)) {
        gx::Header* header = (gx::Header*)rd.ptr();
        const auto msglen = ::ntohs(header->len);

        if (rd.avail() < msglen)
          break;

        // complete header & payload is available; we can now mutate header
        // byte(ntoh) because they will all be discarded after the following
        // call to parse the full message
        header->len = ::ntohs(header->len);
        header->flags = ::ntohs(header->flags);
        header->id = ::ntohs(header->id);
        this->io_on_full_message(header, header->payload,
                                 header->len - sizeof(gx::Header));
        rd.advance(msglen); // note, use msglen, instead of header->len, just in
                            // case was changed.
      }

      // detect inbound DecodeBuffer overflow
      if ((src_len > 0) && (consumed == 0) && (rd.consumed() == 0)) {
        throw std::runtime_error("GX connection inbound DecodeBuffer overflow");
      }

      _buf.discard(rd); /* shift unused bytes to front of DecodeBuffer */
    }
  }

  DecodeBuffer _buf;

protected:
  EventHandlers _callbacks;
};

} // namespace apex
