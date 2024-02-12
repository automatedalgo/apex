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
#include <apex/infra/IoLoop.hpp>
#include <apex/infra/SocketAddress.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>

#include <uv.h>

#include <assert.h>

namespace apex
{

constexpr std::chrono::seconds TcpSocket::options::default_keep_alive_delay;

tcp_socket_guard::tcp_socket_guard(std::unique_ptr<TcpSocket>& __sock)
  : sock(__sock)
{
}

tcp_socket_guard::~tcp_socket_guard()
{
  /* If the TcpSocket object still exists (and so it headed for deletion), is
   * not closed, and current thread is the IO thread, then takeaway ownerhip
   * from the reference-target and request close & deletion via the IO
   * thread. This has to be done because it is not safe to delete an un-closed
   * TcpSocket via the IO thread. */
  if (sock && !sock->is_closed() && sock->get_io_loop().this_thread_is_io()) {
    TcpSocket* ptr = sock.release();
    ptr->close([ptr]() { delete ptr; });
  }
}

const char* TcpSocket::to_string(socket_state s)
{
  switch (s) {
    case socket_state::uninitialised:
      return "uninitialised";
    case socket_state::connecting:
      return "connecting";
    case socket_state::connected:
      return "connected";
    case socket_state::connect_failed:
      return "connect_failed";
    case socket_state::listening:
      return "listening";
    case socket_state::closing:
      return "closing";
    case socket_state::closed:
      return "closed";
  }
  return "unknown";
}

struct write_req {
  // C style polymorphism. The uv_write_t must be first member.
  uv_write_t req;
  uv_buf_t* bufs;
  size_t nbufs;
  size_t total_bytes;
  write_req(size_t n, size_t total)
    : bufs(new uv_buf_t[n]), nbufs(n), total_bytes(total)
  {
  }

  ~write_req()
  {
    for (size_t i = 0; i < nbufs; i++)
      delete[] bufs[i].base;
    delete[] bufs;
  }

  write_req(const write_req&) = delete;
  write_req& operator=(const write_req&) = delete;
};


static void iohandle_alloc_buffer(uv_handle_t* /* handle */,
                                  size_t suggested_size, uv_buf_t* buf)
{
  // improve memory efficiency
  *buf = uv_buf_init((char*)new char[suggested_size], suggested_size);
}


TcpSocket::options::options()
  : tcp_no_delay_enable(default_tcp_no_delay_enable),
    keep_alive_enable(default_keep_alive_enable),
    keep_alive_delay(default_keep_alive_delay)
{
}


TcpSocket::TcpSocket(IoLoop& io_loop_, uv_tcp_t* h, socket_state ss,
                     options opts)
  : _io_loop(io_loop_),
    _sockopts(opts),
    _state(ss),
    _tcp(h),
    _io_closed_promise(new std::promise<void>),
    _io_closed_future(_io_closed_promise->get_future()),
    _bytes_pending_write(0),
    _bytes_written(0),
    _bytes_read(0),
    m_self(this, [](TcpSocket*) { /* none deleter */ })
{
  if (_tcp) {
    assert(_tcp->data == nullptr);
    _tcp->data = new HandleData(this);

    // established-socket is ready, so apply options
    apply_socket_options(false);
  }
}


TcpSocket::TcpSocket(IoLoop& io_loop_, options opts)
  : TcpSocket(io_loop_, nullptr, socket_state::uninitialised, opts)
{
}


TcpSocket::~TcpSocket()
{
  bool io_loop_ended = false;

  LOG_DEBUG("~TcpSocket ("<< this->fd_info().second << "/" <<this->get_local_port() << ":" <<  this->get_peer_port()<< "/" << to_string(_state)<<")");

  {
    /* Optionally initiate close */
    std::lock_guard<std::mutex> guard(_state_lock);
    if ((_state != socket_state::closing) &&
        (_state != socket_state::closed)) {

      _state = socket_state::closing;

      try {
        _io_loop.push_fn([this]() { this->begin_close(); });
      } catch (IoLoopClosed&) {
        io_loop_ended = true;
      }
    }
  }

  if (!is_closed()) {
    /* detect & caution undefined behaviour */
    if (io_loop_ended) {
      LOG_ERROR("undefined behaviour calling TcpSocketfor unclosed socket "
                "when IO loop closed");
    } else if (_io_loop.this_thread_is_io()) {
      LOG_ERROR("undefined behaviour calling TcpSocketfor unclosed socket "
                "on IO thread");
    } else
      _io_closed_future.wait();
  }

  if (_tcp) {
    delete (HandleData*)_tcp->data;
    delete _tcp;
  }

  {
    std::lock_guard<std::mutex> guard(_pending_write_lock);
    for (auto& i : _pending_write)
      delete[] i.base;
  }
}


bool TcpSocket::is_listening() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state == socket_state::listening;
}


bool TcpSocket::is_connected() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state == socket_state::connected;
}


bool TcpSocket::is_connect_failed() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state == socket_state::connect_failed;
}


bool TcpSocket::is_closing() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state == socket_state::closing;
}


bool TcpSocket::is_closed() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state == socket_state::closed;
}


std::future<UvErr> TcpSocket::connect(std::string addr, int port)
{
  return connect(addr, std::to_string(port), addr_family::unspec, true);
}


void TcpSocket::begin_close(bool no_linger)
{
  /* IO thread */

  // this method should only ever be called once by the IO thread, either
  // triggered by pushing a close request or a call from uv_walk.
  {
    std::lock_guard<std::mutex> guard(_state_lock);
    _state = socket_state::closing;
  }

  // decouple from IO request that might still be pending on the IO thread
  m_self.reset();

  if (_tcp) {

#ifndef _WIN32
    uv_os_fd_t fd;
    if (no_linger && (uv_fileno((uv_handle_t*)_tcp, &fd) == 0)) {
      struct linger so_linger;
      so_linger.l_onoff = 1;
      so_linger.l_linger = 0;
      setsockopt(fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
    }
#else
    SOCKET sock = m_uv_tcp->socket;
    if (no_linger && sock != INVALID_SOCKET) {
      struct linger so_linger;
      so_linger.l_onoff = 1;
      so_linger.l_linger = 0;
      setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char*)&so_linger,
                 sizeof so_linger);
    }
#endif

    uv_close((uv_handle_t*)_tcp, [](uv_handle_t* h) {
      /* IO thread, invoked upon uv_close completion */
      HandleData* ptr = (HandleData*)h->data;
      ptr->tcp_socket_ptr()->close_impl();
    });
  } else
    close_impl();
}


void TcpSocket::close_impl()
{
  decltype(_user_close_fn) user_close_fn;
  decltype(_io_closed_promise) closed_promise;

  /* Once the state is set to closed, this TcpSocket object may be immediately
   * deleted by another thread. So this must be the last action that makes use
   * of the TcpSocket members. */
  {
    std::lock_guard<std::mutex> guard(_state_lock);
    _state = socket_state::closed;

    /* Extract from the TcpSocket the child objects that need to have their
     * lifetime extended beyond that of the parent TcpSocket, so that after
     * setting of socket state to e_closed, these objects can still be
     * used. It is also important that the user-close-fn is copied inside
     * this critical section, and not before the lock is taken.*/
    user_close_fn = std::move(_user_close_fn);
    closed_promise = std::move(_io_closed_promise);
  }

  /* Run the user callback first, and then set the promise (the promise is set
   * as the last action, so that an owner of TcpSocket can wait on a future to
   * know when all callbacks are complete). This user callback must not perform
   * a wait on the future, because that only gets set after the callback
   * returns. */
  if (user_close_fn)
    try {
      user_close_fn();
    } catch (...) {
    }
  closed_promise->set_value();
}


std::pair<bool, std::string> TcpSocket::fd_info() const
{
  uv_os_fd_t fd;
  if (_tcp && uv_fileno((uv_handle_t*)_tcp, &fd) == 0) {
    std::ostringstream oss;
    oss << fd;
    return {true, oss.str()};
  } else {
    return {false, ""};
  }
}


/** User request to close socket */
std::shared_future<void> TcpSocket::close()
{
  std::lock_guard<std::mutex> guard(_state_lock);

  if (_state != socket_state::closing && _state != socket_state::closed) {
    _state = socket_state::closing;
    _io_loop.push_fn([this]() { this->begin_close(); }); // can throw
  }

  return _io_closed_future;
}


/** User request to reset & close a socket */
std::shared_future<void> TcpSocket::reset()
{
  std::lock_guard<std::mutex> guard(_state_lock);

  if (_state != socket_state::closing && _state != socket_state::closed) {
    _state = socket_state::closing;
    _io_loop.push_fn([this]() { this->begin_close(true); }); // can throw
  }

  return _io_closed_future;
}


bool TcpSocket::close(on_close_cb user_on_close_fn)
{
  /* Note that it is safe for this to be called when state is e_closing.  In
   * such a situation the on-close callback is due to be invoked very soon (on
   * the IO thread), but because we hold the lock here, the callback function
   * can be altered before it gets invoked (see the uv_close callback).
   */

  std::lock_guard<std::mutex> guard(_state_lock);

  // if TcpSocket is already closed, it will not be possible to later invoke
  // the user provided on-close callback, so return false
  if (_state == socket_state::closed)
    return false;

  _user_close_fn = user_on_close_fn;

  if (_state != socket_state::closing) {
    _state = socket_state::closing;
    _io_loop.push_fn([this]() { this->begin_close(); }); // can throw
  }

  return true;
}


std::future<UvErr> TcpSocket::start_read(io_on_read on_read,
                                         io_on_error on_error)
{
  {
    std::lock_guard<std::mutex> guard(_state_lock);
    if (_state != socket_state::connected)
      throw TcpSocket::error("TcpSocket::start_read() when not connected");
  }

  auto completion_promise = std::make_shared<std::promise<UvErr>>();

  auto fn = [this, completion_promise]() {
    UvErr ec =
        uv_read_start((uv_stream_t*)this->_tcp, iohandle_alloc_buffer,
                      [](uv_stream_t* uvh, ssize_t nread, const uv_buf_t* buf) {
                        auto* ptr = (HandleData*)uvh->data;
                        ptr->tcp_socket_ptr()->on_read_cb(nread, buf);
                      });
    completion_promise->set_value(ec);
  };

  _io_on_read = std::move(on_read);
  _io_on_error = std::move(on_error);

  _io_loop.push_fn(std::move(fn));

  return completion_promise->get_future();
}


void TcpSocket::reset_listener()
{
  _io_on_read = nullptr;
  _io_on_error = nullptr;
}


/* Push a close event, but unlike the user facing function 'close', does not
 * throw an exception if already has been requested to close.
 */
void TcpSocket::close_once_on_io()
{
  /* IO thread */

  std::lock_guard<std::mutex> guard(_state_lock);
  if (_state != socket_state::closing && _state != socket_state::closed) {
    _state = socket_state::closing;
    _io_loop.push_fn([this]() { this->begin_close(); });
  }
}

void TcpSocket::handle_read_bytes(ssize_t nread, const uv_buf_t* buf)
{
  LOG_DEBUG("fd: " << fd_info().second << ", tcp_rx: len " << nread
                   << (nread > 0 ? ", hex " : "")
                   << (nread > 0 ? to_hex(buf->base, nread) : ""));

  if (nread >= 0 && _io_on_read)
    _io_on_read(buf->base, nread);
  else if (nread < 0 && _io_on_error)
    _io_on_error(UvErr(nread));
}

void TcpSocket::on_read_cb(ssize_t nread, const uv_buf_t* buf)
{
  /* IO thread */
  if (nread > 0)
    _bytes_read += nread;

  try {
    handle_read_bytes(nread, buf);
  } catch (...) {
    log_exception("IO thread in on_read_cb");
  }

  delete[] buf->base;
}


void TcpSocket::write(const char* src, size_t len)
{
  uv_buf_t buf;

  scope_guard buf_guard([&buf]() { delete[] buf.base; });

  buf = uv_buf_init(new char[len], len);
  memcpy(buf.base, src, len);

  {
    std::lock_guard<std::mutex> guard(_state_lock);
    if (_state == socket_state::closing || _state == socket_state::closed)
      throw TcpSocket::error("TcpSocket::write() when closing or closed");

    {
      std::lock_guard<std::mutex> guard2(_pending_write_lock);
      _pending_write.push_back(buf);
      buf_guard.release();
    }

    _io_loop.push_fn([this]() { service_pending_write(); });
  }
}


void TcpSocket::write(std::pair<const char*, size_t>* srcbuf, size_t count)
{
  // improve memory usage here
  std::vector<uv_buf_t> bufs;

  scope_guard buf_guard([&bufs]() {
    for (auto& i : bufs)
      delete[] i.base;
  });

  bufs.reserve(count);
  for (size_t i = 0; i < count; i++) {
    uv_buf_t buf = uv_buf_init(new char[srcbuf->second], srcbuf->second);
    memcpy(buf.base, srcbuf->first, srcbuf->second);
    srcbuf++;
    bufs.push_back(buf);
  }

  {
    std::lock_guard<std::mutex> guard(_state_lock);
    if (_state == socket_state::closing || _state == socket_state::closed)
      throw TcpSocket::error("TcpSocket::write() when closing or closed");

    {
      std::lock_guard<std::mutex> guard(_pending_write_lock);
      _pending_write.insert(_pending_write.end(), bufs.begin(), bufs.end());
      bufs.clear();
      buf_guard.release();
    }

    _io_loop.push_fn([this]() { service_pending_write(); });
  }
}


void TcpSocket::do_write(std::vector<uv_buf_t>& bufs)
{
  /* IO thread */
  assert(_io_loop.this_thread_is_io() == true);

  scope_guard buf_guard([&bufs]() {
    for (auto& i : bufs)
      delete[] i.base;
  });

  size_t bytes_to_send = 0;
  for (auto& buf : bufs)
    bytes_to_send += buf.len;

  const size_t pend_max = _sockopts.default_socket_max_pending_write_bytes;

  if (is_connected() && !bufs.empty()) {
    if (bytes_to_send > (pend_max - _bytes_pending_write)) {
      LOG_WARN("pending bytes limit reached; closing connection");
      close_once_on_io();
      return;
    }

    // build the request
    auto* wr = new write_req(bufs.size(), bytes_to_send);
    wr->req.data = this;
    for (size_t i = 0; i < bufs.size(); i++)
      wr->bufs[i] = bufs[i];

    _bytes_pending_write += bytes_to_send;

    int r = uv_write((uv_write_t*)wr, (uv_stream_t*)_tcp, wr->bufs,
                     wr->nbufs, [](uv_write_t* req, int status) {
                       TcpSocket* the_tcp_socket = (TcpSocket*)req->data;
                       the_tcp_socket->on_write_cb(req, status);
                     });
    buf_guard.release();

    if (r) {
      LOG_WARN("uv_write failed, errno " << std::abs(r) << " ("
                                         << uv_strerror(r)
                                         << "); closing connection");
      delete wr;
      close_once_on_io();
      return;
    };
  }
}

void TcpSocket::do_write()
{
  /* IO thread */
  assert(_io_loop.this_thread_is_io() == true);

  std::vector<uv_buf_t> copy;
  {
    std::lock_guard<std::mutex> guard(_pending_write_lock);
    _pending_write.swap(copy);
  }

  scope_guard buf_guard([&copy]() {
    for (auto& i : copy)
      delete[] i.base;
  });

  size_t bytes_to_send = 0;
  for (size_t i = 0; i < copy.size(); i++)
    bytes_to_send += copy[i].len;

  if (LOG_LEVEL_ENABLED(Logger::level::debug)) {
    for (size_t i = 0; i < copy.size(); i++)
      LOG_DEBUG(
          "fd: " << fd_info().second << ", tcp_tx: len " << copy[i].len
                 << (copy[i].len > 0 ? ", hex " : "")
                 << (copy[i].len > 0 ? to_hex(copy[i].base, copy[i].len) : ""));
  }

  const size_t pend_max = _sockopts.default_socket_max_pending_write_bytes;

  if (is_connected() && !copy.empty()) {
    if (bytes_to_send > (pend_max - _bytes_pending_write)) {
      LOG_WARN("pending bytes limit reached; closing connection");
      close_once_on_io();
      return;
    }

    // build the request
    write_req* wr = new write_req(copy.size(), bytes_to_send);
    wr->req.data = this;
    for (size_t i = 0; i < copy.size(); i++)
      wr->bufs[i] = copy[i];

    _bytes_pending_write += bytes_to_send;

    int r = uv_write((uv_write_t*)wr, (uv_stream_t*)_tcp, wr->bufs,
                     wr->nbufs, [](uv_write_t* req, int status) {
                       TcpSocket* the_tcp_socket = (TcpSocket*)req->data;
                       the_tcp_socket->on_write_cb(req, status);
                     });
    buf_guard.release();

    if (r) {
      LOG_WARN("uv_write failed, errno " << std::abs(r) << " ("
                                         << uv_strerror(r)
                                         << "); closing connection");
      delete wr;
      close_once_on_io();
      return;
    }
  }
}


void TcpSocket::on_write_cb(uv_write_t* req, int status)
{
  /* IO thread */

  std::unique_ptr<write_req> wr((write_req*)req); // ensure deletion

  try {
    if (status == 0) {
      size_t total = wr->total_bytes;
      /*
      for (size_t i = 0; i < req->nbufs; i++)
        total += req->bufsml[i].len;
      */
      _bytes_written += total;
      if (_bytes_pending_write > total)
        _bytes_pending_write -= total;
      else
        _bytes_pending_write = 0;
    } else {
      /* write failed - this can happen if we actively terminated the socket
         while there were still a long queue of bytes awaiting output (eg inthe
         case of a slow consumer) */
      close_once_on_io();
    }
  } catch (...) {
    log_exception("IO thread in on_write_cb");
  }
}


/**
 * Called on the IO thread when a new socket is available to be accepted.
 */
void TcpSocket::on_listen_cb(int status)
{
  /* IO thread */
  UvErr ec{status};

  if (ec) {
    _accept_fn(ec, nullptr);
    return;
  }

  uv_tcp_t* client = new uv_tcp_t();
  assert(client->data == 0);
  uv_tcp_init(_io_loop.uv_loop(), client);

  ec = uv_accept((uv_stream_t*)_tcp, (uv_stream_t*)client);
  if (ec == 0) {
    auto new_sock = _accept_fn(0, client);
    if (new_sock) // user callback did not take ownership of socket
    {
      TcpSocket* ptr = new_sock.release();
      ptr->close([ptr]() { delete ptr; });
    }
  } else {
    uv_close((uv_handle_t*)client, free_socket);
  }
}


bool TcpSocket::is_initialised() const
{
  std::lock_guard<std::mutex> guard(_state_lock);
  return _state != socket_state::uninitialised;
}


std::future<UvErr> TcpSocket::listen_impl(const std::string& node,
                                          const std::string& service,
                                          addr_family af,
                                          acceptor_fn_t accept_fn)
{
  assert(_accept_fn == nullptr); // dont allow multiple listen() calls
  _accept_fn = std::move(accept_fn);

  {
    std::lock_guard<std::mutex> guard(_details_lock);
    _node = node;
    _service = service;
  }

  auto completion_promise = std::make_shared<std::promise<UvErr>>();

  _io_loop.push_fn([this, node, service, af, completion_promise]() {
    this->do_listen(node, service, af, completion_promise);
  });

  return completion_promise->get_future();
}

std::future<UvErr> TcpSocket::listen(const std::string& node,
                                     const std::string& service,
                                     on_accept_cb user_accept_fn,
                                     addr_family af)
{
  {
    std::lock_guard<std::mutex> guard(_state_lock);
    if (_state != socket_state::uninitialised)
      throw TcpSocket::error("TcpSocket::listen() when already initialised");
  }

  if (!user_accept_fn)
    throw TcpSocket::error("on_accept_cb is none");

  auto accept_fn = [this, user_accept_fn](UvErr ec, uv_tcp_t* h) {
    std::unique_ptr<TcpSocket> new_sock(
        h ? create(_io_loop, h, socket_state::connected, _sockopts) : 0);

    /* Catch user function exceptions, to prevent stack unwind and destruction
     * of the open TcpSocket object.  The TcpSocket cannot be closed & deleted
     * on the IO thread. */
    try {
      user_accept_fn(new_sock, ec);
    } catch (std::exception& e) {
      LOG_ERROR("exception during socket on_accept_cb : " << e.what());
    } catch (...) {
      LOG_ERROR("exception during socket on_accept_cb : unknown");
    }

    return new_sock;
  };

  return listen_impl(node, service, af, accept_fn);
}


void TcpSocket::do_listen(const std::string& node, const std::string& service,
                          addr_family af,
                          std::shared_ptr<std::promise<UvErr>> completion)
{
  /* IO thread */

#ifndef NDEBUG
  assert(_tcp == nullptr);
  {
    std::lock_guard<std::mutex> guard(_state_lock);
    assert(_state == socket_state::uninitialised);
  }
#endif

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));

  unsigned int tcp_bind_flags = 0;
  switch (af) {
    case addr_family::unspec:
      hints.ai_family = AF_UNSPEC;
      break;
    case addr_family::inet4:
      hints.ai_family = AF_INET;
      break;
    case addr_family::inet6:
      hints.ai_family = AF_INET6;
      /* This is needed, otherwise UV listens to both IPv6 and IPv4 */
      tcp_bind_flags |= UV_TCP_IPV6ONLY;
      break;
  }

  hints.ai_socktype = SOCK_STREAM; /* Connection based socket */
  hints.ai_flags = AI_PASSIVE;     /* Allow wildcard IP address */
  hints.ai_protocol = IPPROTO_TCP;

  /* getaddrinfo() returns a list of address structures that can be used in
   * later calls to bind or connect */
  uv_getaddrinfo_t req;
  UvErr ec =
      uv_getaddrinfo(_io_loop.uv_loop(), &req, nullptr /* no callback */,
                     node.empty() ? nullptr : node.c_str(),
                     service.empty() ? nullptr : service.c_str(), &hints);

  if (ec) {
    completion->set_value(ec);
    return;
  }

  /* Try each address until we successfullly bind. On any error we close the
   * socket and try the next address. */
  uv_tcp_t* h = nullptr;
  struct addrinfo* ai = nullptr;
  for (ai = req.addrinfo; ai != nullptr; ai = ai->ai_next) {

    h = new uv_tcp_t();
    assert(h->data == 0);
    if (uv_tcp_init(_io_loop.uv_loop(), h) != 0) {
      delete h;
      continue;
    }

    if (uv_tcp_bind(h, ai->ai_addr, tcp_bind_flags) == 0)
      break; /* success */

    uv_close((uv_handle_t*)h, free_socket);
  }

  uv_freeaddrinfo(req.addrinfo);

  if (ai == nullptr) {
    /* no address worked, report an approporiate error code */
    completion->set_value(UV_EADDRNOTAVAIL);
    return;
  }

  _tcp = h;
  _tcp->data = new HandleData(this);

  ec = uv_listen((uv_stream_t*)h, 128, [](uv_stream_t* server, int status) {
    HandleData* uvhd_ptr = (HandleData*)server->data;
    uvhd_ptr->tcp_socket_ptr()->on_listen_cb(status);
  });

  if (ec) {
    _tcp = nullptr;
    uv_close((uv_handle_t*)h, free_socket);
  } else {
    std::lock_guard<std::mutex> guard(_state_lock);
    _state = socket_state::listening;

    // listen-socket is ready, so apply options
    apply_socket_options(true);
  }

  completion->set_value(ec);
}


std::future<UvErr> TcpSocket::connect(const std::string& node,
                                      const std::string& service,
                                      addr_family af, bool resolve_addr)
{
  {
    std::lock_guard<std::mutex> guard(_state_lock);

    if (_state != socket_state::uninitialised)
      throw TcpSocket::error("TcpSocket::connect() when already initialised");

    _state = socket_state::connecting;
  }

  {
    std::lock_guard<std::mutex> guard(_details_lock);
    _node = node;
    _service = service;
  }

  auto completion_promise = std::make_shared<std::promise<UvErr>>();

  _io_loop.push_fn(
      [this, node, service, af, resolve_addr, completion_promise]() {
        this->do_connect(node, service, af, resolve_addr, completion_promise);
      });

  return completion_promise->get_future();
}


struct connect_context {
  uv_connect_t request; // must be first, allow for casts
  std::shared_ptr<std::promise<UvErr>> completion;
  std::weak_ptr<TcpSocket> wp;

  connect_context(std::shared_ptr<std::promise<UvErr>> p,
                  std::weak_ptr<TcpSocket> sock)
    : completion(p), wp(std::move(sock))
  {
  }
};


void TcpSocket::do_connect(const std::string& node, const std::string& service,
                           addr_family af, bool resolve_addr,
                           std::shared_ptr<std::promise<UvErr>> completion)
{
  /* IO thread */

  assert(_tcp == nullptr);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));

  switch (af) {
    case addr_family::unspec:
      // This connects to either IPv4 and IPv6
      hints.ai_family = AF_UNSPEC;
      break;
    case addr_family::inet4:
      hints.ai_family = AF_INET;
      break;
    case addr_family::inet6:
      hints.ai_family = AF_INET6;
      break;
  }

  hints.ai_socktype = SOCK_STREAM; /* Connection based socket */
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_socktype = resolve_addr ? 0 : (AI_NUMERICHOST | AI_NUMERICSERV);

  /* getaddrinfo() returns a list of address structures that can be used in
   * later calls to bind or connect */
  uv_getaddrinfo_t req;
  UvErr ec =
      uv_getaddrinfo(_io_loop.uv_loop(), &req, nullptr /* no callback */,
                     node.empty() ? nullptr : node.c_str(),
                     service.empty() ? nullptr : service.c_str(), &hints);

  if (ec) {
    completion->set_value(ec);
    return;
  }

  /* Try each address until a call to connect is successful. On any error we
   * close the socket and try the next address. */
  uv_tcp_t* h = nullptr;
  struct addrinfo* ai = nullptr;
  for (ai = req.addrinfo; ai != nullptr; ai = ai->ai_next) {

    h = new uv_tcp_t();
    assert(h->data == 0);
    if (uv_tcp_init(_io_loop.uv_loop(), h) != 0) {
      delete h;
      continue;
    }
    h->data = new HandleData(HandleData::handle_type::tcp_connect);

    auto* ctx = new connect_context(completion, m_self);

    ec = uv_tcp_connect(
        (uv_connect_t*)ctx, h, ai->ai_addr, [](uv_connect_t* req, int status) {
          std::unique_ptr<connect_context> ctx((connect_context*)req);

          if (auto sp = ctx->wp.lock()) {
            sp->connect_completed(status, ctx->completion,
                                  (uv_tcp_t*)req->handle);
          } else {
            /* We no longer have a reference to the original TcpSocket.  This
             * happens when the TcpSocket object has been deleted before the
             * uv_connect callback was called.  We have no use for the current
             * uv_tcp_t handle, so just delete.  We also check that the handle
             * is not already closing, which may be the case if the IO loop has
             * been shutdown.
             */
            if (!uv_is_closing((uv_handle_t*)req->handle))
              uv_close((uv_handle_t*)req->handle, free_socket);
          }
        });

    if (ec == 0)
      break; /* success, connect in progress */

    delete ctx;
    uv_close((uv_handle_t*)h, free_socket);
  }

  uv_freeaddrinfo(req.addrinfo);

  if (ai == nullptr) {
    /* no address worked, use the last error code seen if non-zero */
    completion->set_value(ec ? ec : UV_EADDRNOTAVAIL);
    return;
  }

  /* Note: completion is only set during the connect_completed callback */
}


void TcpSocket::connect_completed(
    UvErr ec, std::shared_ptr<std::promise<UvErr>> completion, uv_tcp_t* h)
{
  /* IO thread */

  std::lock_guard<std::mutex> guard(_state_lock);

  /* State might be closed/closing, which can happen if a TcpSocket is deleted
   * before the a previous connect attempt has completed. */
  assert(_tcp == nullptr);
  assert(_state == socket_state::connecting || _state == socket_state::closing);

  if (ec == 0) {
    _state = socket_state::connected;
    _tcp = h;
    auto ptr = (HandleData*)_tcp->data;
    *ptr = HandleData(this);

    // established-socket is ready, so apply options
    apply_socket_options(false);
  } else {
    _state = socket_state::connect_failed;
    uv_close((uv_handle_t*)h, free_socket);
  }

  completion->set_value(ec);
}

void TcpSocket::service_pending_write() { do_write(); }

TcpSocket* TcpSocket::create(IoLoop& l, uv_tcp_t* h, socket_state s,
                             options opts)
{
  return new TcpSocket(l, h, s, opts);
}

const std::string& TcpSocket::node() const
{
  std::lock_guard<std::mutex> guard(_details_lock);
  return _node;
}

const std::string& TcpSocket::service() const
{
  std::lock_guard<std::mutex> guard(_details_lock);
  return _service;
}


SocketAddress TcpSocket::get_peer_address()
{
  if (!_tcp)
    return SocketAddress();

  SocketAddress sa;
  int ss_len = sizeof(::sockaddr_storage);

  static_assert(std::is_same<SocketAddress::impl_type::element_type,
                             ::sockaddr_storage>(),
                "types are not the same");
  static_assert(std::is_same<decltype(sa._impl.get()), ::sockaddr_storage*>(),
                "types are not the same");

  uv_tcp_getpeername(_tcp, (sockaddr*)sa._impl.get(), &ss_len);

  return sa;
}


int TcpSocket::get_peer_port()
{
  if (!_tcp)
    return 0;

  ::sockaddr_storage ss;
  int ss_len = sizeof ss;

  uv_tcp_getpeername(_tcp, (sockaddr*)&ss, &ss_len);

  if (ss.ss_family == AF_INET) {
    ::sockaddr_in* addrin = (::sockaddr_in*)&ss;
    return ntohs(addrin->sin_port);
  }

  if (ss.ss_family == AF_INET6) {
    ::sockaddr_in6* addrin6 = (::sockaddr_in6*)&ss;
    return ntohs(addrin6->sin6_port);
  }

  return 0;
}


SocketAddress TcpSocket::get_local_address()
{
  if (!_tcp)
    return SocketAddress();

  SocketAddress sa;
  int ss_len = sizeof(::sockaddr_storage);

  static_assert(std::is_same<SocketAddress::impl_type::element_type,
                             ::sockaddr_storage>(),
                "types are not the same");
  static_assert(std::is_same<decltype(sa._impl.get()), ::sockaddr_storage*>(),
                "types are not the same");

  uv_tcp_getsockname(_tcp, (sockaddr*)sa._impl.get(), &ss_len);

  return sa;
}


int TcpSocket::get_local_port()
{
  if (!_tcp)
    return 0;

  ::sockaddr_storage ss;
  int ss_len = sizeof ss;

  uv_tcp_getsockname(_tcp, (sockaddr*)&ss, &ss_len);

  if (ss.ss_family == AF_INET) {
    ::sockaddr_in* addrin = (::sockaddr_in*)&ss;
    return ntohs(addrin->sin_port);
  }

  if (ss.ss_family == AF_INET6) {
    ::sockaddr_in6* addrin6 = (::sockaddr_in6*)&ss;
    return ntohs(addrin6->sin6_port);
  }

  return 0;
}


void TcpSocket::apply_socket_options(bool is_listen_socket)
{
  UvErr ec;

  /* it likely only makes sense to apply some socket options to established
   * connections, rather that listen sockets */
  if (!is_listen_socket) {
    ec = uv_tcp_nodelay(_tcp, _sockopts.tcp_no_delay_enable);
    if (ec)
      LOG_WARN("uv_tcp_nodelay failed, " << ec.message());

    ec = uv_tcp_keepalive(_tcp, _sockopts.keep_alive_enable,
                          _sockopts.keep_alive_delay.count());
    if (ec)
      LOG_WARN("uv_tcp_keepalive failed, " << ec.message());
  }
}

} // namespace apex
