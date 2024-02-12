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

#include <apex/infra/UvErr.hpp>

#include <future>
#include <iostream>
#include <string>
#include <vector>

#include <string.h>

// VC++ doesn't define ssize_t, so follow definition used by libuv
#ifdef _WIN32
#if !defined(_SSIZE_T_) && !defined(_SSIZE_T_DEFINED)
typedef intptr_t ssize_t;
#define _SSIZE_T_
#define _SSIZE_T_DEFINED
#endif
#endif

/* Types defined as part of the libuv.  We don't want to include any libuv
 * headers here, so instead use declarations to dependencies. */
struct uv_buf_t;
struct uv_tcp_s;
struct uv_write_s;
typedef struct uv_tcp_s uv_tcp_t;
typedef struct uv_write_s uv_write_t;

namespace apex
{

class IoLoop;
class SocketAddress;
class TcpSocket;

/** A RAII utility class to manage the lifetime of a block-scope TcpSocket
 * resource.  The guard will ensure that, at scope termination, the TcpSocket
 * is correctly disposed of, either via normal destruction at scope end, or, via
 * later destruction on the IO thread. This guard implements the
 * TcpSocket ownership rule, i.e., an unclosed socket should not be deleted on
 * the IO thread. */
class tcp_socket_guard
{
public:
  explicit tcp_socket_guard(std::unique_ptr<TcpSocket>& __sock);
  ~tcp_socket_guard();
  std::unique_ptr<TcpSocket>& sock;
};


/**
A TCP socket with both server and client operation.

A socket is created and run in either server mode or in client mode.  Server
mode involves use of the listen() method; in client mode uses connect().

The owner of a TcpSocket must take care during its deletion.  It is
undefined behaviour to invoke the TcpSocket destructor via the internal IO
thread for an instance not in the closed state.

There are three ways to achieve safe deletion of a TcpSocket:

1. Invoke the destructor on a thread other than the IO thread. It is always safe
   to delete a TcpSocket this way, no matter what the current state of the
   instance.  The destructor will, if necessary, advance the state to closed and
   perform any wait required for closed to be reached.

2. Manually ensure the instance has reached the closed state before calling the
   destructor.  The close of a TcpSocket is initiated by calling close().  Once
   initiated the socket will transition to the closed state asynchronously.  The
   caller can wait for closed to be reached by waiting on the future returned by
   close() or closed_future(). Such a wait must not be performed on the IO
   thread, since it is the IO thread that performs the internal work required to
   advance the state to closed. The method is_closed() tells if the closed state
   has been reached.

3. Delete the socket asynchronously using a callback associated with the close
   operation.  The close(on_close_cb) method initiates close of a TcpSocket;
   once the closed state has been achieved the user callback is invoked (via the
   IO thread).  Now that the close state has been reached the instance
   can be safely deleted during the callback.

In addition to these strategies a tcp_socket_guard can be used to ensure the
correct disposal of a TcpSocket that has a scoped lifetime. The guard will
detect if an unclosed TcpSocket is about to be deleted on the IO thread,
and in such situations will intervene to delete the instance via asynchronous
callback.

Using a tcp_socket_guard with a scoped TcpSocket prevents accidental unsafe
deletion.  Unintended deletion typically occurs when an exception is thrown that
leads to scope exit, and with it the deletion of associated local objects.
*/
class TcpSocket
{
public:
  enum class addr_family {
    /* support both IPv6/IPv4 */
    unspec,
    /* IPv4 only */
    inet4,
    /* IPv6 only */
    inet6
  };

  /** Type thrown by TcpSocket when actions are attempted when the socket is
   * not in an appropriate state. */
  class error : public std::runtime_error
  {
  public:
    explicit error(std::string msg) : std::runtime_error(msg) {}
  };


  /* Socket options to apply to newly created sockets. This is not an exhaustive
   * set, instead just covers most common options. */
  struct options {
    /* Default values */
    static constexpr bool default_tcp_no_delay_enable = true;
    static constexpr bool default_keep_alive_enable = true;
    static constexpr std::chrono::seconds default_keep_alive_delay =
        std::chrono::seconds(60);
    static constexpr long default_socket_max_pending_write_bytes =
        0x100000; // 1mb

    /* Individual options */
    bool tcp_no_delay_enable;

    bool keep_alive_enable;
    std::chrono::seconds keep_alive_delay;

    options();
  };

  typedef std::function<void(char*, size_t)> io_on_read;
  typedef std::function<void(UvErr)> io_on_error;
  typedef std::function<void()> on_close_cb;
  typedef std::function<void(std::unique_ptr<TcpSocket>&, UvErr)> on_accept_cb;

  TcpSocket(IoLoop&, options = {});
  virtual ~TcpSocket();

  IoLoop& get_io_loop() { return _io_loop; }

  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  /** Request TCP connection to a remote end point, using IPv4 and allowing DNS
   * resolution.  This should only be called on an uninitialised socket. */
  virtual std::future<UvErr> connect(std::string addr, int port);

  /** Request TCP connection to a remote end point.  This should only be called
   * on an uninitialised socket. */
  virtual std::future<UvErr> connect(const std::string& node,
                                     const std::string& service,
                                     addr_family = addr_family::unspec,
                                     bool resolve_addr = true);

  /** Request socket begins reading inbound data, with callbacks made on the IO
   * thread. */
  virtual std::future<UvErr> start_read(io_on_read, io_on_error);

  /** Reset IO callbacks */
  void reset_listener();

  /** Initialise this instance by creating a listen socket that is bound to
   * an end point. The user callback is called when an incoming
   * connection request is accepted. Node can be the empty string, in which case
   * the listen socket will accept incoming connections from all interfaces
   * (i.e. INADDR_ANY). */
  std::future<UvErr> listen(const std::string& node, const std::string& service,
                            on_accept_cb, addr_family = addr_family::unspec);

  /* Request a write */
  void write(std::pair<const char*, size_t>* srcbuf, size_t count);
  void write(const char*, size_t);

  /** Request asynchronous socket close. To detect when close has occurred, the
   * caller can wait upon the returned future.  Throws IoLoopClosed if IO loop
   * has already been closed. */
  std::shared_future<void> close();

  /** Request asynchronous socket reset & close.  */
  std::shared_future<void> reset();

  /** Request asynchronous socket close, and get notification via the
   * specified callback on the IO thread. If the TcpSocket is not currently
   * closed then the provided callback is invoked at the time of socket closure
   * and true is returned.  Otherwise, if the socket is already closed, the
   * callback is never invoked and false is returned. Throws IoLoopClosed if
   * IO loop has already been closed. */
  bool close(on_close_cb);

  /** Obtain future that is set only when this TcpSocket is closed for future
   * callback events.*/
  std::shared_future<void> closed_future() const { return _io_closed_future; }

  bool is_connected() const;
  bool is_connect_failed() const;
  bool is_listening() const;
  bool is_closing() const;

  /** Return whether the closed state has been reached */
  bool is_closed() const;

  /** Return whether this TcpSocket has been initialised, which means it is
   * associated with an underlying socket file descriptor (until closed). */
  bool is_initialised() const;

  /** Return description of the underlying file description, if one is currently
   * associated with this TcpSocket. The first member of the pair indicates if
   * the fd is available. */
  std::pair<bool, std::string> fd_info() const;

  size_t bytes_read() const { return _bytes_read; }
  size_t bytes_written() const { return _bytes_written; }

  /** Return the node name, as provided during the connect / listen call. */
  const std::string& node() const;

  /** Return the service name, as provided during the connect / listen call. */
  const std::string& service() const;

  /** Get local-socket address */
  SocketAddress get_local_address();

  /** Get local-socket port */
  int get_local_port();

  /** Get peer-socket address */
  SocketAddress get_peer_address();

  /** Get peer-socket port */
  int get_peer_port();

protected:
  enum class socket_state {
    uninitialised,
    connecting,
    connected,
    connect_failed,
    listening,
    closing,
    closed
  };

  TcpSocket(IoLoop&, uv_tcp_t*, socket_state ss, options);

  virtual void handle_read_bytes(ssize_t, const uv_buf_t*);
  virtual void service_pending_write();

  typedef std::function<std::unique_ptr<TcpSocket>(UvErr ec, uv_tcp_t* h)>
      acceptor_fn_t;
  void do_write(std::vector<uv_buf_t>&);
  std::future<UvErr> listen_impl(const std::string&, const std::string&,
                                 addr_family, acceptor_fn_t);

  IoLoop& _io_loop;

  options _sockopts;

  /* Store user requests to write bytes. These are queued until handled by
   * the IO thread, via service_pending_write(). */
  std::vector<uv_buf_t> _pending_write;
  std::mutex _pending_write_lock;

  /* User callbacks. */
  io_on_read _io_on_read;
  io_on_error _io_on_error;

  socket_state _state;
  mutable std::mutex _state_lock;

  mutable std::mutex _details_lock;
  std::string _node;
  std::string _service;

private:
  static TcpSocket* create(IoLoop&, uv_tcp_t*, socket_state, options);
  void close_impl();

  static const char* to_string(TcpSocket::socket_state);

  void on_read_cb(ssize_t, const uv_buf_t*);
  void on_write_cb(uv_write_t*, int);
  void close_once_on_io();
  void do_write();
  void begin_close(bool no_linger = false);
  void do_listen(const std::string&, const std::string&, addr_family,
                 std::shared_ptr<std::promise<UvErr>>);
  void do_connect(const std::string&, const std::string&, addr_family, bool,
                  std::shared_ptr<std::promise<UvErr>>);
  void connect_completed(UvErr, std::shared_ptr<std::promise<UvErr>>,
                         uv_tcp_t*);
  void on_listen_cb(int);

  void apply_socket_options(bool);

  uv_tcp_t* _tcp;

  std::unique_ptr<std::promise<void>> _io_closed_promise;
  std::shared_future<void> _io_closed_future;

  std::atomic<size_t> _bytes_pending_write;
  size_t _bytes_written;
  size_t _bytes_read;

  on_close_cb _user_close_fn;

  std::shared_ptr<TcpSocket> m_self;

  /* Handler for creating a new instance when a socket is accepted. */
  acceptor_fn_t _accept_fn;

  friend IoLoop;
};

} // namespace apex
