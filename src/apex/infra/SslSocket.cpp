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
#include <apex/infra/TcpConnector.hpp>
#include <apex/core/Logger.hpp>
#include <apex/infra/ssl.hpp>
#include <apex/util/utils.hpp>

#include <cassert>

#define DEFAULT_BUF_SIZE 4096

namespace apex
{


struct Buffer
{
  Buffer(const char* buf, size_t n)
    : _data(buf, buf+n),
      _consumed(0) {}

  size_t avail() const { return _data.size() - _consumed; }

  char* data() { return _data.data() + _consumed; }

  void mark_consumed(size_t n) { _consumed += n; }

  std::vector<char> _data;
  size_t _consumed;
};


template<typename T>
std::string print_ssl_state(T & ssl)
{
  const char * current_state = SSL_state_string_long(ssl->ssl);
  printf("SSL-STATE: %s\n", current_state);
  return std::string(current_state);
}


SslSocket::SslSocket(SslContext* ssl_context, Reactor* r)
  : TcpSocket(r),
    _ssl_context(ssl_context),
    _handshake_state(handshake_state_t::pending)
{
  if (_stream)
    _stream->on_write_cb = [this]()-> ssize_t { return this->ssl_do_write();};
}


SslSocket::SslSocket(SslContext* ssl_context, Reactor* r, int fd)
  : TcpSocket(r, fd),
    _ssl_context(ssl_context),
    _handshake_state(handshake_state_t::pending)
{
  if (_stream)
    _stream->on_write_cb = [this]()-> ssize_t { return this->ssl_do_write();};
}


SslSocket::~SslSocket()
{
  // the _stream must be disposed before beginning to destroy self, otherwise io
  // thread might try to use destroyed resources in derived object
  _reactor->detach_stream_unique_ptr(_stream);
}


sslstatus SslSocket::ssl_handshake()
{
  /* io-thread */

  char buf[DEFAULT_BUF_SIZE];

  int n = SSL_do_handshake(_ssl_session->ssl);

  sslstatus status = get_sslstatus(_ssl_session->ssl, n);

  if (status == sslstatus::fail) {
    if (_handshake_state == handshake_state_t::pending) {
      _handshake_state = handshake_state_t::failed;
      LOG_ERROR("fd:" << fd() << ", SSL handshake failed, " << _ssl_context->get_ssl_errors());
    }
    return status;
  }

  if (status == sslstatus::want_io) { // SSL has bytes to send to peer
    do {
      n = BIO_read(_ssl_session->wbio, buf, sizeof(buf));
      if (n > 0) {
        this->TcpSocket::write(buf, n);
      }
      else if (!BIO_should_retry(_ssl_session->wbio)) {
        return sslstatus::fail;
      }
    } while (n > 0);
  }

  if (SSL_is_init_finished(_ssl_session->ssl) &&
      _handshake_state == handshake_state_t::pending) {
    _handshake_state = handshake_state_t::success;

    // there might be outbound data waiting to be encrypted, if it was written
    // before the handshake was complete
    bool pending_outbound_data = false;
    {
      std::lock_guard<std::mutex> guard(_encryptbuf_mtx);
      pending_outbound_data = !_encrypt.empty();
    }
    if (pending_outbound_data)
      _reactor->start_write(_stream.get());
  }

  return status;
}


/* Pass raw bytes from the socket into SSL for decryption. */
int SslSocket::ssl_do_read(char* src, size_t len)
{
  /* io-thread */

  char buf[DEFAULT_BUF_SIZE];

  while (len > 0) {
    int n = BIO_write(_ssl_session->rbio, src, len);

    if (n <= 0)
      return -1; // treat mem bio write error as unrecoverable

    len -= n;
    src += n;

    // maybe contine handshake
    if (!SSL_is_init_finished(_ssl_session->ssl)) {
      if (ssl_handshake() == sslstatus::fail)
        return -1; // unrecoverable, close connection

      /* If we are still not initialised, then perhaps there is more data to
       * writbe into the read-bio? Check by continuing the loop. */
      if (!SSL_is_init_finished(_ssl_session->ssl)) {
        continue;
      }
    }

    /* Encrypted data is now in the read bio, so use SSL_read() to obtain the
     * decrypted data, which is then passed to user. */
    do {
      n = SSL_read(_ssl_session->ssl, buf, sizeof(buf));
      if (n > 0)
        _user_on_read(buf, n);
    } while (n > 0);

    sslstatus status = get_sslstatus(_ssl_session->ssl, n);

    // SSL wants to write? This can happen if peer wants renegotiation.
    if (status == sslstatus::want_io)
      do {
        n = BIO_read(_ssl_session->wbio, buf, sizeof(buf));
        if (n > 0)
          this->TcpSocket::write(buf, n);
        else if (!BIO_should_retry(_ssl_session->wbio)) {
          print_ssl_state(_ssl_session);
          return -1;
        }
      } while (n > 0);

    if (status == sslstatus::fail) {
      _ssl_context->log_ssl_error_queue();
      return -1;
    }
  }

  return 0; // success
}


void SslSocket::start_read(on_read_cb_t user_cb)
{
  assert (user_cb);
  assert (!_user_on_read);
  _user_on_read = user_cb;
  _stream->on_read_cb = [this](char* data, ssize_t n) {
    if (n > 0){
      if (this->ssl_do_read(data, n) < 0)
        this->close();
    }
    else
      this->_user_on_read(data, n);
  };
  _reactor->start_read(_stream.get());
}


SslSocket::write_err SslSocket::write(const char* buf, size_t n)
{
  if (!is_open())
    return write_err::no_socket;

  {
    std::lock_guard<std::mutex> guard(_encryptbuf_mtx);
    _encrypt.push_back(Buffer(buf, n));
  }

  _reactor->start_write(_stream.get());
  return write_err::success;
}


SslSocket::write_err SslSocket::write(std::string_view sv)
{
  return this->write(sv.data(), sv.size());
}


ssize_t SslSocket::ssl_do_write()
{
  /* io-thread */

  // pull in any pending user bytes; even if none, we don't want to exit early
  // because we still want to service any write that is pending
  std::list<Buffer> encrypt;
  {
    std::lock_guard<std::mutex> guard(_encryptbuf_mtx);
    encrypt.swap(_encrypt);
  }

  for (auto it = encrypt.begin(); it != encrypt.end(); ++it) {
    while (it->avail()) {

      int nwrote = do_encrypt_and_write(it->data(), it->avail());
      if (nwrote > 0) {
        it->mark_consumed(nwrote);
      }
      else if (nwrote < 0) {
        LOG_ERROR("fd:" << fd() << ", SSL failed, " <<  _ssl_context->get_ssl_errors());
        return -1; // fail the socket
      }
      else
        break; // nwrote==0, SSL_write didn't take all data
    }

    if (it->avail()) {
      // Some data was not passed to the SSL object, so put all remaing data
      // back on the outbound queue
      {
        std::lock_guard<std::mutex> guard(_encryptbuf_mtx);
        for (auto it = encrypt.rbegin(); it != encrypt.rend(); ++it) {
          if (it->avail())
            _encrypt.push_front(std::move(*it));
        }
      }
      break; // break for-loop
    }
  }

  return wants_write()? this->TcpSocket::do_write() : 0;
}


/* Attempt to encrypt a single block of data, by putting it through the SSL
 * object, and then take the output (representing the encrypted data) and queue
 * for socket write. Returns first==-1 on failure. */
int SslSocket::do_encrypt_and_write(char* src, size_t srclen)
{
  char buf[DEFAULT_BUF_SIZE];

  if (!SSL_is_init_finished(_ssl_session->ssl)) {
    if (ssl_handshake() == sslstatus::fail)
      return -1; // fail
    if (!SSL_is_init_finished(_ssl_session->ssl)) {
      return 0; // nothing written
    }
  }

  // write the user data into the SSL, for encryption
  int nwrote = SSL_write(_ssl_session->ssl, src, srclen);
  if (nwrote <= 0 && (get_sslstatus(_ssl_session->ssl, nwrote) == sslstatus::fail))
    return -1;

  // extract any encrypted bytes available in the SSL, and pass to socket
  int n;
  do {
    n = BIO_read(_ssl_session->wbio, buf, sizeof(buf));
    if (n > 0)
      this->TcpSocket::write(buf, n);
    else if (!BIO_should_retry(_ssl_session->wbio))
      return -1;
  } while (n > 0);

  return nwrote;
}


void SslSocket::connect(std::string addr,
                        int port,
                        int timeout,
                        connect_complete_cb_t user_cb)
{
  if (_connector)
    throw std::runtime_error("cannot connect() after listen()");

  _ssl_session = std::make_unique<SslSession>(_ssl_context,
                                              connect_mode::connect);

  this->_node = addr;
  this->_service = std::to_string(port);

  auto completed_cb = [this, user_cb](int fd, int err) {
                                /* io-thread */
    if (fd != NULL_FD) {
      auto on_write_cb = [this]() -> ssize_t {return this->ssl_do_write();};
      this->set_connected_fd(fd, on_write_cb);

      // Initiate the SSL handshake, just in the cause doesn't request to write
      // any bytes (which itself would trigger SSL handshake).  Note that for
      // the handshake to succeed, the user will need to call start_read.
      this->ssl_handshake();
    }
    if (user_cb)
      user_cb((fd == NULL_FD)? (err>0? err: EPERM) : 0);
  };

  // create the TcpConnector object, which will manage the connection process
  _connector = std::make_unique<TcpConnector>(_reactor, std::move(completed_cb));

  // initiate connection
  _connector->connect(addr, port, timeout);
}


void SslSocket::listen(int port, ssl_on_accept_cb_t user_on_accept_cb)
{
  if (_ssl_session)
    throw std::runtime_error("cannot listen(), SslSession already created");

  if (!user_on_accept_cb)
    throw std::runtime_error("cannot listen(), accept callback is empty");

  create_sock_cb_t create_sock_cb = [user_on_accept_cb, this](int fd){
    if (fd >= 0) {
      auto sock = std::make_unique<SslSocket>(_ssl_context, _reactor, fd);
      sock->_ssl_session = std::make_unique<SslSession>(_ssl_context,
                                                        connect_mode::accept);
      user_on_accept_cb(sock);
    }
  };

  this->listen_impl(port, create_sock_cb);
}


} // namespace apex
