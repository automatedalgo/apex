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

namespace apex
{

struct SslSession;
enum class sslstatus;
class SslContext;

/**
 * Represent an SSL TCP socket, in both server mode and client mode.
 */
class SslSocket : public TcpSocket
{
public:
  enum class t_handshake_state { pending, success, failed };

  typedef std::function<void(std::unique_ptr<SslSocket>&, UvErr)>
      ssl_on_accept_cb;

  SslSocket(SslContext&, IoLoop&, TcpSocket::options = {});
  ~SslSocket();
  SslSocket(const SslSocket&) = delete;
  SslSocket& operator=(const SslSocket&) = delete;

  /* Request the asynchronous send of the SSL client handshake.  This can be
   * called after connect() has successfully completed.  Note that this does not
   * have to be called. Any attempt to write data to a SSL connection which has
   * not completed the handshake will cause a handshake attempt to be
   * automatically made. Should not be called more than once. */
  std::future<t_handshake_state> handshake();

  /* Has the initial SSL handshake been completed? */
  t_handshake_state handshake_state();

  /** Initialise this SslSocket by creating a listen socket that is bound to
   * the specified end point. The user callback is called when an incoming
   * connection request is accepted. Node can be the empty string, in which case
   * the listen socket will accept incoming connections from all interfaces
   * (i.e. INADDR_ANY). */
  std::future<UvErr> listen(const std::string& node, const std::string& service,
                            ssl_on_accept_cb,
                            addr_family = addr_family::unspec);

  std::future<UvErr> connect(std::string addr, int port) override;

  std::future<UvErr> connect(const std::string& node,
                             const std::string& service,
                             addr_family = addr_family::unspec,
                             bool resolve_addr = true) override;
private:
  SslSocket(SslContext&, IoLoop&, uv_tcp_t*, socket_state ss,
            TcpSocket::options);

  void handle_read_bytes(ssize_t, const uv_buf_t*) override;
  void service_pending_write() override;
  static SslSocket* create(SslContext&, IoLoop&, uv_tcp_t*,
                           TcpSocket::socket_state, TcpSocket::options);

  std::pair<int, size_t> do_encrypt_and_write(char*, size_t);
  sslstatus do_handshake();
  void write_encrypted_bytes(const char* src, size_t len);
  int ssl_do_read(char* src, size_t len);

  SslContext& _ssl_context;
  std::unique_ptr<SslSession> _ssl;
  t_handshake_state _handshake_state;
  std::promise<t_handshake_state> _prom_handshake;
};

} // namespace apex
