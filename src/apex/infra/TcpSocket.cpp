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

#include <apex/infra/TcpSocket.hpp>
#include <apex/infra/TcpConnector.hpp>
#include <apex/infra/SocketAddress.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>

#include <uv.h>

#include <assert.h>

static int BACKLOG = 50;

void abort_with_msg(const char* msg, const char* reason) {
  fprintf(stderr, "%s: %s\n", msg, reason);
  abort();

}

namespace apex
{

TcpSocket::TcpSocket(Reactor* r)
  : _reactor(r),
    _outbuf_n(0)
{
}


TcpSocket::TcpSocket(Reactor* r, int fd)
  : _reactor(r),
    _stream(std::make_unique<TcpStream>(fd)),
    _outbuf_n(0)
{
  _stream->user = this;
  _stream->on_write_cb = [this]() -> ssize_t {
    return this->do_write();
  };

  // note: on registering with the reactor, we can immediately receive IO
  // callbacks
  _reactor->add_stream(_stream.get());
}



TcpSocket::~TcpSocket() {
  _reactor->detach_stream_unique_ptr(_stream);
}

/* Is this socket currently associated with an open file descriptor? */
bool TcpSocket::is_open() const {
  return _stream && _stream->has_fd() && !_stream->err;
}


void TcpSocket::start_read(on_read_cb_t cb) {
  assert (_stream.get() != nullptr);
  assert (cb);
  assert (!_stream->on_read_cb);

  _stream->on_read_cb = cb;
  _reactor->start_read(_stream.get());
}


TcpSocket::write_err TcpSocket::write(const char* buf, size_t n)
{
  if (!is_open())
    return write_err::no_socket;

  {
    std::lock_guard<std::mutex> guard(_outbuf_mtx);

    if (n > (_outbuf.size() - _outbuf_n))
      return write_err::no_space;

    memcpy(_outbuf.data()+_outbuf_n, buf, n);
    _outbuf_n += n;
  }

  _reactor->start_write(_stream.get());
  return write_err::success;
}


TcpSocket::write_err TcpSocket::write(std::string_view sv)
{
  return this->write(sv.data(), sv.size());
}

bool TcpSocket::wants_write() {
  std::lock_guard<std::mutex> guard(_outbuf_mtx);
  return _outbuf_n > 0;
}


ssize_t TcpSocket::do_write()
{
  /* io-thread */

  /* After write attempts, one of the following will be true:

     1. all queued data has been sent   -> stop polling (0)
     2. some queued data has been sent  -> poll again (1)
     3. socket error                    -> close socket (-1)
  */

  std::lock_guard<std::mutex> guard(_outbuf_mtx);
  char * p = _outbuf.data();
  int n;

  while (_outbuf_n > 0) {
    do {
      // use send() instead of write() to prevent SIGPIPE events
      n = ::send(_stream->fd, p, _outbuf_n, MSG_DONTWAIT|MSG_NOSIGNAL);
    } while (n == -1 && errno == EINTR);

    // LOG_INFO("SENT: " << n);

    if (n > 0) {
      _outbuf_n -= n;
      p += n;
    }
    else {
      memmove(_outbuf.data(), p, _outbuf_n);
      if (n == 0)
        return 1;
      else {
        _stream->write_err = (errno == EAGAIN || errno == EWOULDBLOCK)? 0 : errno;
        return _stream->write_err ? -1: 1;  // -1=>err, 1=>poll-again
      }
    }
  }
  return 0; // 0=>done,
}


void TcpSocket::close()
{
  _reactor->close_stream(_stream.get());
}

int TcpSocket::fd() const {
  return (_stream && _stream->has_fd())? _stream->fd : -1;
}


int TcpSocket::local_port() const
{
  sockaddr_storage ss; // is at least as large as any other sockaddr_*
  socklen_t ss_len = sizeof ss;

  if (_stream && _stream->has_fd()) {
    if (getsockname(_stream->fd, (sockaddr*) &ss, &ss_len) < 0 )
      return -1;

    if (ss.ss_family == AF_INET) {
      sockaddr_in* addrin = (sockaddr_in*)&ss;
      return ntohs(addrin->sin_port);
    }

    if (ss.ss_family == AF_INET6) {
      sockaddr_in6* addrin6 = (sockaddr_in6*)&ss;
      return ntohs(addrin6->sin6_port);
    }
  }

  return -1;
}


bool TcpSocket::is_connecting() const {
  return _connector && !_connector->is_completed();
}


int TcpSocket::connect_errno() const {
  return _connector->last_errno();
}


void TcpSocket::set_connected_fd(int fd, on_write_cb_t on_write_cb) {
  /* io-thread */
  assert(fd>=0);
  assert(!_stream);

  // construct a Reactor handle, using the connected file descriptor

  _stream = std::make_unique<TcpStream>(fd);
  _stream->user = this;
  _stream->on_write_cb = std::move(on_write_cb);
  _reactor->add_stream(_stream.get());
}


void TcpSocket::connect(std::string addr,
                        int port,
                        int timeout,
                        connect_complete_cb_t user_cb)
{
  assert (!_connector);

  this->_node = addr;
  this->_service = std::to_string(port);

  auto completed_cb = [this, user_cb](int fd, int err) {
    // std::cout << "ON_COMPLETED_CB: fd=" << fd << ", err=" << err << std::endl;

    if (fd != NULL_FD) {
      auto on_write_cb = [this]() -> ssize_t {return this->do_write();};
      this->set_connected_fd(fd, on_write_cb);
    }

    // TODO: instead of passing bad a fake error, can instead pass back a bool,
    // and then exposed a last_err member in TcpSocket.

    // if user wants a callback, pass back the error, making sure that we have
    // an error code.
    if (user_cb)
      user_cb((fd == NULL_FD)? (err>0? err: EPERM) : 0);
  };


  // create the TcpConnector object, which will manage the connection process
  _connector = std::make_unique<TcpConnector>(_reactor, std::move(completed_cb));

  // initiate connection
  _connector->connect(addr, port, timeout);
}


void TcpSocket::listen_impl(int port, create_sock_cb_t create_sock_cb)
{
  if (_stream)
    throw std::runtime_error("cannot listen(), socket already initialised");

  std::string port_str = std::to_string(port);

  struct ::addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM; /* Connection based socket */
  hints.ai_flags = AI_PASSIVE; /* Allow wildcard IP address */
  hints.ai_protocol = IPPROTO_TCP;

  struct ::addrinfo * result;

  // TODO: handle EAI_AGAIN .. basically just retry
  int s = ::getaddrinfo(NULL, port_str.c_str(), &hints, &result);
  if (s != 0)
    abort_with_msg("getaddrinfo", gai_strerror(s));

  /* getaddrinfo() returns a list of address structures. Try each address until
     we successfully bind(2).  If socket(2) (or bind(2)) fails, we (close the
     socket and) try the next address. */

  int sfd = -1;
  int last_err = 0;

  for (struct ::addrinfo * rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1) {
      last_err = errno;
      continue;
    }

    int value = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (&value), sizeof(value));

    if (::bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
      break; /* Success */
    else {
      last_err = errno;
      // LOG_WARN("bind failed: " << err_to_string(errno));
      ::close(sfd);
      sfd = -1;
    }
  }
  freeaddrinfo(result);

  if (sfd == -1)
    throw std::system_error{last_err, std::system_category(), "socket/bind"};

  // TODO: check for EINTR
  if (::listen(sfd, BACKLOG) < 0) {
    last_err = errno;
    ::close(sfd);
    throw std::system_error{last_err, std::system_category(), "socket/listen"};
  }

  auto cb = [create_sock_cb](Stream* stream, int /* unused */){
    /* io-thread */
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);

    // TODO: handle EINTR here?
    int fd = ::accept4(stream->fd, (sockaddr*)&addr, &len, SOCK_NONBLOCK);
    if (fd == -1) {
      perror("Accept failed");
    }
    else
      create_sock_cb(fd);
  };

  // create TcpStream
  _stream = std::make_unique<TcpStream>(sfd);
  _stream->user = this;
  _stream->on_write_cb = [this]() -> ssize_t {
    LOG_WARN("ignoring socket-write attempt for listening socket");
    return -1;
  };
  _stream->on_connection_cb = std::move(cb);
  _reactor->add_stream(_stream.get());
  _reactor->start_accept(_stream.get());
}


void TcpSocket::listen_impl(const std::string& node,
                            const std::string& service,
                            create_sock_cb_t create_sock_cb)
{
  if (_stream)
    throw std::runtime_error("cannot listen(), socket already initialised");

  if (node.empty() && service.empty())
    throw std::runtime_error("listen() requires at least node or service");

  struct ::addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM; /* Connection based socket */
  hints.ai_flags = AI_PASSIVE; /* Allow wildcard IP address */
  hints.ai_protocol = IPPROTO_TCP;

  struct ::addrinfo * result;

  // TODO: handle EAI_AGAIN .. basically just retry
  int s = ::getaddrinfo(node.empty()? nullptr:node.c_str(),
                        service.empty()? nullptr:service.c_str(),
                        &hints, &result);
  if (s != 0)
    abort_with_msg("getaddrinfo", gai_strerror(s));

  /* getaddrinfo() returns a list of address structures. Try each address until
     we successfully bind(2).  If socket(2) (or bind(2)) fails, we (close the
     socket and) try the next address. */

  int sfd = -1;
  int last_err = 0;

  for (struct ::addrinfo * rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1) {
      last_err = errno;
      continue;
    }

    int value = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (&value), sizeof(value));

    if (::bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
      break; /* Success */
    else {
      last_err = errno;
      // LOG_WARN("bind failed: " << err_to_string(errno));
      ::close(sfd);
      sfd = -1;
    }
  }
  freeaddrinfo(result);

  if (sfd == -1)
    throw std::system_error{last_err, std::system_category(), "socket/bind"};

  // TODO: check for EINTR
  if (::listen(sfd, BACKLOG) < 0) {
    last_err = errno;
    ::close(sfd);
    throw std::system_error{last_err, std::system_category(), "socket/listen"};
  }

  auto cb = [create_sock_cb](Stream* stream, int /* unused */){
    /* io-thread */
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(addr);

    // TODO: handle EINTR here?
    int fd = ::accept4(stream->fd, (sockaddr*)&addr, &len, SOCK_NONBLOCK);
    if (fd == -1) {
      perror("Accept failed");
    }
    else
      create_sock_cb(fd);
  };

  // create TcpStream
  _stream = std::make_unique<TcpStream>(sfd);
  _stream->user = this;
  _stream->on_write_cb = [this]() -> ssize_t {
    LOG_WARN("ignoring socket-write attempt for listening socket");
    return -1;
  };
  _stream->on_connection_cb = std::move(cb);
  _reactor->add_stream(_stream.get());
  _reactor->start_accept(_stream.get());
}


void TcpSocket::listen(int port, on_accept_cb_t user_on_accept_cb)
{
  if (!user_on_accept_cb)
    throw std::runtime_error("cannot listen(), accept callback is empty");

  create_sock_cb_t create_sock_cb = [user_on_accept_cb, this](int fd){
    if (fd >= 0) {
      auto sock = std::make_unique<TcpSocket>(this->_reactor, fd);
      user_on_accept_cb(sock);
    }
  };

  this->listen_impl(port, create_sock_cb);
}


void TcpSocket::listen(const std::string& node,
                       const std::string& port,
                       on_accept_cb_t user_on_accept_cb)
{
  if (!user_on_accept_cb)
    throw std::runtime_error("cannot listen(), accept callback is empty");

  create_sock_cb_t create_sock_cb = [user_on_accept_cb, this](int fd){
    if (fd >= 0) {
      auto sock = std::make_unique<TcpSocket>(this->_reactor, fd);
      user_on_accept_cb(sock);
    }
  };

  this->listen_impl(node, port, create_sock_cb);
}



} // namespace apex
