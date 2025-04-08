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

#include <apex/infra/TcpSocket.hpp>

#include <list>

namespace apex
{

struct SslSession;
enum class sslstatus;
class SslContext;
  class SslSocket;

class Buffer;

using ssl_on_accept_cb_t = std::function<void(std::unique_ptr<SslSocket>&)>;

class SslSocket : public TcpSocket {
public:

  /* Create an uninitialised socket */
  SslSocket(SslContext*, Reactor*);

  /* Create from an existing file descriptor */
  SslSocket(SslContext*, Reactor*, int fd);

  virtual ~SslSocket();

  virtual void connect(std::string addr, int port, int timeout,
                       connect_complete_cb_t = nullptr);

  /* Set this socket to listen */
  virtual void listen(int port, ssl_on_accept_cb_t on_accept_cb);

  /* Write data */
  write_err write(const char*, size_t) override;
  write_err write(std::string_view) override;

  virtual void start_read(on_read_cb_t) override;

private:
  ssize_t ssl_do_write();
  int ssl_do_read(char*, size_t);
  sslstatus ssl_handshake();
  int do_encrypt_and_write(char* src, size_t len);

  SslContext* _ssl_context;
  std::unique_ptr<SslSession> _ssl_session;

  enum class handshake_state_t {pending, success, failed} _handshake_state ;

  // outbound bytes awaiting encryption
  std::mutex _encryptbuf_mtx;
  std::list<Buffer> _encrypt;

  on_read_cb_t _user_on_read;
};


} // namespace apex
