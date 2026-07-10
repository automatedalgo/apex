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

#include <apex/net/Reactor.hpp>

#include <future>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>


namespace apex
{
  class TcpConnector;

  class TcpSocket {
  public:

    static constexpr int DEFAULT_RECV_BUF_LEN = 10240; // 10K

    using on_accept_cb_t = std::function<void(std::unique_ptr<TcpSocket>&)>;
    using connect_complete_cb_t = std::function<void(int)>;

    enum class write_err : int {
      success = 0,
      no_socket = 1,
      no_space = 2
    };

    /* Create an uninitialised socket */
    explicit TcpSocket(Reactor*);

    /* Create from an existing file-descriptor, used when a next file descriptor
     * has arrived from a listening socket that has accepted a new
     * connection. */
    TcpSocket(Reactor*, int fd, size_t read_buf_len);

    virtual ~TcpSocket();

    virtual void connect(std::string addr, int port, int timeout,
                         connect_complete_cb_t);

    /* Start listening for connections */
    virtual void listen(int port, on_accept_cb_t on_accept_cb);

    /* Start listening for connections */
    virtual void listen(const std::string& host,
                        const std::string& service,
                        on_accept_cb_t on_accept_cb);

    /* Write data */
    virtual write_err write(const char*, size_t) ;
    virtual write_err write(std::string_view) ;

    /* Start polling for events, with callbacks made on the IO thread. */
    virtual void start_read(on_read_cb_t);

    /* Is socket trying to connect? */
    [[nodiscard]] bool is_connecting() const;

    /* If socket was trying to connect, but connect failed, return the errno. */
    [[nodiscard]] int connect_errno() const;

    /* Is this socket currently associated with an open file descriptor? */
    [[nodiscard]] bool is_open() const;

    /* Close socket */
    void close();

    /* Return the file descriptor of this socket, or -1 if not available */
    [[nodiscard]] int fd() const;

    /* Get local port */
    [[nodiscard]] int local_port() const;

    [[nodiscard]] std::string node() const { return _node; }
    [[nodiscard]] std::string service() const { return _service; }

    /* Set the initial value for the socket receive buffer size.  Must be called
     * before a connection is established. */
    void set_recv_buf_len(size_t n) { _init_read_buf_len = n; }

    TimeLog& timelog() { return _stream->tlog; }

  protected:
    using create_sock_cb_t = std::function<void(int)>;
    ssize_t do_write();
    void set_connected_fd(int, on_write_cb_t);
    void listen_impl(const std::string& node, const std::string& port, create_sock_cb_t cb);
    bool wants_write();

  protected:
    Reactor * _reactor;
    std::unique_ptr<TcpStream> _stream;

    std::mutex _outbuf_mtx;
    std::array<char, 1024*100> _outbuf;
    size_t _outbuf_n;

    size_t _init_read_buf_len;

    std::string _node;
    std::string _service;

    std::unique_ptr<TcpConnector> _connector;
  };

} // namespace apex
