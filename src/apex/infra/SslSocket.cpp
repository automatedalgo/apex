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

#include <apex/infra/SslSocket.hpp>
#include <apex/infra/IoLoop.hpp>
#include <apex/core/Logger.hpp>
#include <apex/infra/ssl.hpp>
#include <apex/util/utils.hpp>

#include <cassert>

#define DEFAULT_BUF_SIZE 4096

/* For SSL failure, represent that during the user on_error callback with a
 * suitable UV error code. */
#define SSL_UV_FAIL UV_EPROTO

namespace apex
{

/* Return a new uv_buf_t containing a copy of a sub-region of the source,
   starting at offset 'pos' */
static uv_buf_t sub_buf(uv_buf_t& src, size_t pos)
{
  uv_buf_t buf = uv_buf_init((char*)new char[src.len - pos], src.len - pos);
  memcpy(buf.base, src.base + pos, buf.len);
  return buf;
}


SslSocket::SslSocket(SslContext& sslContext, IoLoop& io_loop_, uv_tcp_t* h,
                     socket_state ss, TcpSocket::options options)
  : TcpSocket(io_loop_, h, ss, options),
    _ssl_context(sslContext),
    _ssl(new SslSession(&sslContext, connect_mode::passive)),
    _handshake_state(t_handshake_state::pending)
{
}


SslSocket::SslSocket(SslContext& sslContext, IoLoop& io_loop_,
                     TcpSocket::options options)
  : TcpSocket(io_loop_, options),
    _ssl_context(sslContext),
    _ssl(new SslSession(&sslContext, connect_mode::active)),
    _handshake_state(t_handshake_state::pending)
{
}


SslSocket::~SslSocket() = default;


std::future<UvErr> SslSocket::listen(const std::string& node,
                                     const std::string& service,
                                     ssl_on_accept_cb user_accept_fn,
                                     addr_family af)
{
  if (is_initialised())
    throw TcpSocket::error("SslSocket::listen() when already initialised");

  if (!user_accept_fn)
    throw TcpSocket::error("ssl_on_accept_cb is none");


  auto accept_fn = [this, user_accept_fn](UvErr ec, uv_tcp_t* h) {
    std::unique_ptr<SslSocket> up(h ? create(_ssl_context, _io_loop, h,
                                             socket_state::connected, _sockopts)
                                    : 0);

    user_accept_fn(up, ec);

    return std::unique_ptr<TcpSocket>(std::move(up));
  };

  return listen_impl(node, service, af, std::move(accept_fn));
}


/* Service bytes waiting on the m_pending_write queue, which are due to be
 * written out of the SSL socket. These bytes must first be encrypted, and then
 * written to the underlying socket. */
void SslSocket::service_pending_write()
{
  assert(get_io_loop().this_thread_is_io() == true);

  // accept all unencrypted bytes that are waiting to be written
  std::vector<uv_buf_t> bufs;
  {
    std::lock_guard<std::mutex> guard(_pending_write_lock);
    if (_pending_write.empty())
      return;
    _pending_write.swap(bufs);
  }

  scope_guard buf_guard([&bufs]() {
    for (auto& i : bufs)
      delete[] i.base;
  });

  for (auto it = bufs.begin(); it != bufs.end(); ++it) {
    size_t consumed = 0;
    while (consumed < it->len) {
      auto r = do_encrypt_and_write(it->base + consumed, it->len - consumed);

      if (r.first == -1) {
        _io_on_error(UvErr(SSL_UV_FAIL));
        return; /* SSL failed, so okay to discard all objects in 'bufs' */
      }

      if (r.second == 0)
        break; /* SSL_write couldn't accept data */

      consumed += r.second;
    }

    if (consumed < it->len) {
      /* SSL_write failed to fully write a DecodeBuffer, but also, SSL did not
       * report an error.  Seems like some kind of flow control.  We'll keep the
       * unconsumed data, plus other pending buffers, for a later attempt. */
      std::vector<uv_buf_t> tmp{sub_buf(*it, consumed)};
      for (++it; it != bufs.end(); ++it) {
        tmp.push_back(*it);
        it->base = nullptr; /* prevent scope guard freeing the unused bytes */
      }

      std::lock_guard<std::mutex> guard(_pending_write_lock);
      tmp.insert(tmp.end(), _pending_write.begin(), _pending_write.end());
      _pending_write.swap(tmp);

      /* break loop, because iterator has been incremented in loop body,
       * otherwise the it!=end check() can be skipped over */
      break;
    }
  }
}


/* Attempt to encrypt a single block of data, by putting it through the SSL
 * object, and then take the output (representing the encrypted data) and queue
 * for socket write. Returns first==-1 on failure. */
std::pair<int, size_t> SslSocket::do_encrypt_and_write(char* src, size_t len)
{
  assert(get_io_loop().this_thread_is_io() == true);

  char buf[DEFAULT_BUF_SIZE];

  if (!SSL_is_init_finished(_ssl->ssl)) {
    if (do_handshake() == sslstatus::fail)
      return {-1, 0};
    if (!SSL_is_init_finished(_ssl->ssl))
      return {0, 0};
  }

  int w = SSL_write(_ssl->ssl, src, len);
  if (get_sslstatus(_ssl->ssl, w) == sslstatus::fail)
    return {-1, 0};

  /* take the output of the SSL object and queue it for socket write */
  int n;
  do {
    /* If BIO_read successfully obtained data, then n > 0.  A return value
     * of 0 or -1 does not necessarily indicate an error, in particular,
     * when used with our non-blocking memory bio. To check for an error, we
     * must use BIO_should_retry.*/

    n = BIO_read(_ssl->wbio, buf, sizeof(buf));
    if (n > 0)
      write_encrypted_bytes(buf, n);
    else if (!BIO_should_retry(_ssl->wbio))
      return {-1, 0};
  } while (n > 0);

  return {0, w};
}


SslSocket::t_handshake_state SslSocket::handshake_state()
{
  return _handshake_state;
}


std::future<SslSocket::t_handshake_state> SslSocket::handshake()
{
  std::lock_guard<std::mutex> guard(_state_lock);

  if (_state == socket_state::uninitialised)
    throw TcpSocket::error("SslSocket::handshake() before connect");

  if (_state == socket_state::closing || _state == socket_state::closed)
    throw TcpSocket::error("SslSocket::handshake() when closing or closed");

  auto fut = _prom_handshake.get_future();

  get_io_loop().push_fn([this]() { this->do_handshake(); });

  return fut;
}


sslstatus SslSocket::do_handshake()
{
  assert(get_io_loop().this_thread_is_io() == true);

  char buf[DEFAULT_BUF_SIZE];

  int n = SSL_do_handshake(_ssl->ssl);
  sslstatus status = get_sslstatus(_ssl->ssl, n);

  if (status == sslstatus::fail) {
    if (_handshake_state == t_handshake_state::pending) {
      _handshake_state = t_handshake_state::failed;
      _prom_handshake.set_value(_handshake_state);
    }
    return status;
  }

  /* Did SSL request to write bytes? */
  if (status == sslstatus::want_io)
    do {
      n = BIO_read(_ssl->wbio, buf, sizeof(buf));
      if (n > 0)
        write_encrypted_bytes(buf, n);
      else if (!BIO_should_retry(_ssl->wbio))
        return sslstatus::fail;
    } while (n > 0);

  if (SSL_is_init_finished(_ssl->ssl) &&
      _handshake_state == t_handshake_state::pending) {
    _handshake_state = t_handshake_state::success;
    LOG_DEBUG("fd: " << fd_info().second << ", ssl handshake success");
    _prom_handshake.set_value(_handshake_state);
  }

  return status;
}


void SslSocket::write_encrypted_bytes(const char* src, size_t len)
{
  assert(get_io_loop().this_thread_is_io() == true);

  uv_buf_t buf = uv_buf_init(new char[len], len);
  memcpy(buf.base, src, len);

  std::vector<uv_buf_t> bufs{buf};
  do_write(bufs);
}


/* Arrival of raw bytes from the actual socket, if nread>0. These must be passed
 * to SSL for unencryption.  If SSL fails, or there is socket error, call the
 * user error callback.  For EOF (nread==0), invoke standard user callback.
 */
void SslSocket::handle_read_bytes(ssize_t nread, const uv_buf_t* buf)
{
  assert(get_io_loop().this_thread_is_io() == true);

  if (nread > 0 && ssl_do_read(buf->base, size_t(nread)) == 0)
    return; /* data received and successfully fed into SSL */

  if (_handshake_state == t_handshake_state::pending)
    _prom_handshake.set_value(_handshake_state = t_handshake_state::failed);

  if (nread > 0)
    _io_on_error(UvErr(SSL_UV_FAIL));
  else if (nread == 0)
    _io_on_read(nullptr, 0);
  else if (nread < 0 && _io_on_error)
    _io_on_error(UvErr(nread));
}


/* Pass raw bytes from the socket into SSL for unencryption. */
int SslSocket::ssl_do_read(char* src, size_t len)
{
  assert(get_io_loop().this_thread_is_io() == true);

  char buf[DEFAULT_BUF_SIZE];

  while (len > 0) {
    int n = BIO_write(_ssl->rbio, src, len);

    if (n <= 0)
      return -1; /* assume mem bio write error is unrecoverable */

    src += n;
    len -= n;

    /* Handle initial handshake or renegotiation */
    if (!SSL_is_init_finished(_ssl->ssl)) {
      if (do_handshake() == sslstatus::fail) {
        _ssl_context.log_ssl_error_queue();
        return -1;
      }

      /* If we are still not initialised, then perhaps there is more data to
       * write into the read-bio? Check by continue-ing the loop. */
      if (!SSL_is_init_finished(_ssl->ssl))
        continue;
    }

    /* The encrypted data is now in the input bio so now we can perform actual
     * read of unencrypted data. */
    do {
      n = SSL_read(_ssl->ssl, buf, sizeof(buf));
      if (n > 0 && _io_on_read)
        _io_on_read(buf, (size_t)n);
    } while (n > 0);

    sslstatus status = get_sslstatus(_ssl->ssl, n);

    /* Did SSL request to write bytes? This can happen if peer has requested SSL
     * renegotiation. */
    if (status == sslstatus::want_io)
      do {
        n = BIO_read(_ssl->wbio, buf, sizeof(buf));
        if (n > 0)
          write_encrypted_bytes(buf, n);
        else if (!BIO_should_retry(_ssl->wbio))
          return -1;
      } while (n > 0);

    if (status == sslstatus::fail) {
      _ssl_context.log_ssl_error_queue();
      return -1;
    }
  }

  /* In the unlikely event that there are left-over bytes from an incomplete
   * SSL_write that are waiting a retry, make attempt to service them. */
  service_pending_write();

  return 0;
}


/* This is the inherited virtual constructor from TcpSocket, but with a
 * SslSocket return type (C++ covariant types). */
SslSocket* SslSocket::create(SslContext& ssl, IoLoop& k, uv_tcp_t* h,
                             TcpSocket::socket_state s, TcpSocket::options opts)
{
  return new SslSocket(ssl, k, h, s, opts);
}


std::future<UvErr> SslSocket::connect(std::string addr, int port)
{
  auto fut = TcpSocket::connect(addr, port);
  SSL_set_tlsext_host_name(_ssl->ssl, addr.c_str());
  return fut;
}


std::future<UvErr> SslSocket::connect(const std::string& node,
                                      const std::string& service,
                                      addr_family family,
                                      bool resolve_addr)
{
  auto fut = TcpSocket::connect(node,
                                service,
                                family,
                                resolve_addr);
  SSL_set_tlsext_host_name(_ssl->ssl, node.c_str());
  return fut;
}


} // namespace apex
