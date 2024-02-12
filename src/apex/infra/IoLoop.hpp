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
#include <apex/util/utils.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <uv.h>

namespace apex
{

class IoLoop;
class TcpSocket;
struct io_request;

class HandleData
{
public:
  const static uint64_t DATA_CHECK = 0x5555555555555555;

  enum class handle_type { unknown = 0, tcp_socket, tcp_connect };

  HandleData(TcpSocket* ptr)
    : m_check(DATA_CHECK),
      m_type(handle_type::tcp_socket),
      m_tcp_socket_ptr(ptr)
  {
  }

  HandleData(handle_type ht)
    : m_check(DATA_CHECK), m_type(ht), m_tcp_socket_ptr(nullptr)
  {
  }

  uint64_t check() const { return m_check; }
  TcpSocket* tcp_socket_ptr() { return m_tcp_socket_ptr; }
  handle_type type() const noexcept { return m_type; }

private:
  uint64_t m_check; /* retain as first member */
  handle_type m_type;
  TcpSocket* m_tcp_socket_ptr;
};

void free_socket(uv_handle_t* h);

class IoLoopClosed : public std::exception
{
public:
  const char* what() const noexcept override { return "IoLoop closed"; }
};


/* Encapsulate the IO services.  This provides a single instance of
 * the libuv event loop & IO thread. */
class IoLoop
{
public:
  IoLoop(std::function<void()> io_starting_cb = nullptr);
  ~IoLoop();

  /** Perform synchronous stop of the IO loop.  On return, the IO thread will
   * have been joined. */
  void sync_stop();

  void on_async();

  void cancel_connect(uv_tcp_t*);

  /** Push a function for later invocation on the IO thread.  Throws
   * IoLoopClosed if the IO loop is closing or closed.
   */
  void push_fn(std::function<void()>);

  uv_loop_t* uv_loop() { return _uv_loop; }

  /** Test whether current thread is the IO thread */
  bool this_thread_is_io() const;

private:
  void run_loop();

  void on_tcp_connect_cb(uv_connect_t* __req, int status);

  void push_request(std::unique_ptr<io_request>);

  uv_loop_t* _uv_loop;
  std::unique_ptr<uv_async_t> _async;

  enum state { open, closing, closed } _pending_requests_state;
  std::vector<std::unique_ptr<io_request>> _pending_requests;
  std::mutex _pending_requests_lock;

  synchronized_optional<std::thread::id> _io_thread_id;

  std::thread _thread; // prefer as final member, avoid race condition
};

} // namespace apex
