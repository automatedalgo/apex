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

#include <apex/infra/IoLoop.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/core/Logger.hpp>
#include <apex/util/utils.hpp>

#include <system_error>

#include <assert.h>
#include <iostream>

namespace apex
{

void free_socket(uv_handle_t* h)
{
  if (h) {
    delete (HandleData*)h->data;
    delete h;
  }
}

struct io_request {
  enum class request_type {
    cancel_handle,
    close_loop,
    function,
  } type;

  uv_tcp_t* tcp_handle = nullptr;
  std::function<void()> user_fn;

  explicit io_request(request_type __type) : type(__type) {}
};


IoLoop::IoLoop(std::function<void()> io_started_cb)
  : _uv_loop(new uv_loop_t()),
    _async(new uv_async_t()),
    _pending_requests_state(state::open)
{
  uv_loop_init(_uv_loop);
  _uv_loop->data = this;

  // set up the async handler
  uv_async_init(_uv_loop, _async.get(), [](uv_async_t* h) {
    IoLoop* p = static_cast<IoLoop*>(h->data);
    p->on_async();
  });
  _async->data = this;

  // prevent SIGPIPE from crashing application when socket writes are
  // interrupted
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN); // TODO: add support for Windows
#endif

  _thread = std::thread([this, io_started_cb]() {
    scope_guard undo_thread_id([this]() { _io_thread_id.release(); });
    _io_thread_id.set_value(std::this_thread::get_id());
    Logger::instance().register_thread_id("libuv");
    if (io_started_cb)
      try {
        io_started_cb();
      } catch (...) { /* ignore */
      }

    try {
      IoLoop::run_loop();
    } catch (...) { /* ignore */
    }
  });
}


void IoLoop::sync_stop()
{
  std::unique_ptr<io_request> r(
      new io_request(io_request::request_type::close_loop));

  try {
    push_request(std::move(r));
  } catch (IoLoopClosed&) { /* ignore */
  }

  if (_thread.joinable())
    _thread.join();
}


IoLoop::~IoLoop()
{
  uv_loop_close(_uv_loop);
  delete _uv_loop;
}


void IoLoop::on_async()
{
  /* IO thread */
  std::vector<std::unique_ptr<io_request>> work;

  {
    std::lock_guard<std::mutex> guard(_pending_requests_lock);
    work.swap(_pending_requests);
    if (_pending_requests_state == state::closing)
      _pending_requests_state = state::closed;
  }

  for (auto& user_req : work) {
    if (user_req->type == io_request::request_type::cancel_handle) {
      auto handle_to_cancel = (uv_handle_t*)user_req->tcp_handle;
      if (!uv_is_closing(handle_to_cancel))
        uv_close(handle_to_cancel, [](uv_handle_t* handle) { delete handle; });
    } else if (user_req->type == io_request::request_type::close_loop) {
      /* close event handler run at function exit */
    } else if (user_req->type == io_request::request_type::function) {
      user_req->user_fn();
    } else {
      assert(false);
    }
  }

  if (_pending_requests_state == state::closed) {
    uv_close((uv_handle_t*)_async.get(), 0);

    // While there are active handles, progress the event loop here and on
    // each iteration identify and request close any handles which have not
    // been requested to close.
    uv_walk(
        _uv_loop,
        [](uv_handle_t* handle, void* /*arg*/) {
          if (!uv_is_closing(handle)) {
            HandleData* ptr = (HandleData*)handle->data;

            if (ptr == 0) {
              // We are uv_walking a handle which does not have the data member
              // set. Common cause of this is a shutdown of the kernel & ioloop
              // while a connector exists which has not had its UV handle used.
              uv_close(handle, [](uv_handle_t* h) { delete h; });
            } else {
              assert(ptr->check() == HandleData::DATA_CHECK);

              if (ptr->type() == HandleData::handle_type::tcp_socket)
                ptr->tcp_socket_ptr()->begin_close();
              else if (ptr->type() == HandleData::handle_type::tcp_connect)
                uv_close(handle, free_socket);
              else {
                /* unknown handle, so just close it */
                assert(0);
                uv_close(handle, [](uv_handle_t* h) { delete h; });
              }
            }
          }
        },
        nullptr);
  }
}


bool IoLoop::this_thread_is_io() const
{
  return _io_thread_id.compare(std::this_thread::get_id());
}


void IoLoop::run_loop()
{
  while (true) {
    try {
      int r = uv_run(_uv_loop, UV_RUN_DEFAULT);

      if (r == 0) /*  no more handles; we are shutting down */
        return;
    } catch (const std::exception& e) {
      LOG_ERROR("IoLoop exception: " << e.what());
    } catch (...) {
      LOG_ERROR("unknown IoLoop exception");
    }
  }
}


void IoLoop::cancel_connect(uv_tcp_t* handle)
{
  std::unique_ptr<io_request> r(
      new io_request(io_request::request_type::cancel_handle));
  r->tcp_handle = handle;
  push_request(std::move(r));
}


void IoLoop::push_request(std::unique_ptr<io_request> r)
{
  // The io_request represents some IO operation, eg, send data on the
  // socket. This needs to be completed by the IO thread, so here all we do is
  // store the request in pending queue, and notify the libuv-IO thread that we
  // have asynchronous work for it.
  {
    std::lock_guard<std::mutex> guard(_pending_requests_lock);

    if (_pending_requests_state == state::closed)
      throw IoLoopClosed();

    if (r->type == io_request::request_type::close_loop)
      _pending_requests_state = state::closing;

    _pending_requests.push_back(std::move(r));
  }

  uv_async_send(_async.get()); // wake-up IO thread
}


void IoLoop::push_fn(std::function<void()> fn)
{
  std::unique_ptr<io_request> r(
      new io_request(io_request::request_type::function));
  r->user_fn = std::move(fn);
  push_request(std::move(r));
}


void libuv_version_runtime(int& major, int& minor)
{
  // version we are linked to at runtime
  major = (uv_version() & 0xFF0000) >> 16;
  minor = (uv_version() & 0x00FF00) >> 8;
}


} // namespace apex
