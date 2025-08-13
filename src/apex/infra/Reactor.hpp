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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <poll.h>
#include <fcntl.h>
#include <thread>
#include <vector>
#include <queue>
#include <memory>
#include <mutex>
#include <sstream>
#include <functional>
#include <future>
#include <semaphore.h>

#include <apex/util/TimeLog.hpp>

namespace apex {

struct Stream;
class Reactor;

using on_listen_cb_t = std::function<void(Stream*, int)>;
using on_write_cb_t = std::function<ssize_t()>;
using on_connect_cb_t = std::function<void(Stream*, int)>;
using on_read_cb_t = std::function<void(char*, ssize_t)>;

/*
Stream objects are deleted on main thread of their associated reactor.

A user can request a Stream be deleted via the dispose stream.  This sets the
disposing flag, which indicates that it is no longer safe for the reactor to
invoke any of the user callbacks, with the exception of on_dispose_cb; if that
is set, it shall be called, typically to inform (& unblock) the user that the
Stream has been deleted.
*/
struct Stream {

  static constexpr int NULL_FD = -1;

  using on_accept_cb_t = std::function<void(int, struct sockaddr_in*)>;

  int fd;
  int events;
  bool hangup; // got socket hangup
  bool err; // got socket error
  bool eof; // got socket eof
  bool do_close; // do socket close
  bool do_delete; // do delete
  bool disposing; // deletion due, don't invoke user callbacks
  int read_err;
  int write_err;
  on_listen_cb_t on_connection_cb; // TODO: fix this name
  on_read_cb_t on_read_cb;
  on_write_cb_t on_write_cb;
  on_connect_cb_t on_connect_timeout_cb;
  on_accept_cb_t on_accept_cb;
  std::function<void()> on_dispose_cb;
  std::function<void()> user_cb;
  void * user;
  int timeout;  // time in seconds
  TimeLog tlog;

  Stream(int fd, size_t recv_buf_len)
    : fd{fd},
      events(0),
      hangup(false),
      err(false),
      eof(false),
      do_close(false),
      do_delete(false),
      disposing(false),
      read_err(0),
      write_err(0),
      on_connection_cb(nullptr),
      user(nullptr),
      timeout(0),
      _recv_buf(recv_buf_len, 0)
  {
  }

  char * recv_buf() { return _recv_buf.data(); }
  size_t recv_space() const { return _recv_buf.size(); }

  ~Stream();

  bool has_fd() const { return fd != Stream::NULL_FD; }

private:
  std::vector<char> _recv_buf;
};


class TcpStream : public Stream  {
public:
  TcpStream(int fd, size_t recv_buf_len) : Stream(fd, recv_buf_len) {};
};


class Reactor
{
public:
  Reactor();
  ~Reactor();

  void add_stream(Stream*);
  void close_stream(Stream*);
  void detach_stream(Stream*);
  void stream_user_cb(Stream*);
  void start_read(Stream*);
  void start_write(Stream*);
  void start_accept(Stream*);


  template <typename T>
  void detach_stream_unique_ptr(std::unique_ptr< T >& uptr) {
    if (uptr) {
      this->detach_stream(uptr.get());
      uptr.release();
      uptr.reset(nullptr);
    }
  }

  bool is_reactor_thread() const;

public:

  void thread_main();
  void reactor_main_loop();

  int _pipefd[2]; // std::array

  struct Command {

    enum class Type : int
    {
      none = 0,
      user_cb,
      close,
      start_read,
      start_write,
      start_accept,
      add,
      exit,
      dispose
    } type;
    Stream* stream;

  Command(Type type, Stream* s) : type(type), stream(s) {}

  };

private:
  void push_command(Command);

  std::vector<Stream*> _streams;
  std::queue<Command> _commands;
  std::mutex _commands_mtx;
  std::thread::id _thread_id;
  std::thread _thread;
};

}
