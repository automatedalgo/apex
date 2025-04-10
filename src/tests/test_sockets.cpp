/* Copyright 2025 Automated Algo (www.automatedalgo.com)

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

#include "quicktest.hpp"

#include <apex/infra/SslSocket.hpp>
#include <apex/infra/TcpConnector.hpp>
#include <apex/infra/TcpSocket.hpp>
#include <apex/infra/ssl.hpp>

#include <iostream>
#include <future>
#include <list>
#include <cassert>

using namespace std;
using namespace apex;

#define LOG_INFO( X ) do { std::cout << "info: " << X << std::endl;} while (0);
#define LOG_WARN( X ) do { std::cout << "warning: "<< X << std::endl;} while (0);
#define LOG_ERROR( X ) do { std::cout << "error: "<< X << std::endl;} while (0);

std::string MSG = "HELLO_WORLD_ABCDEFGHIJKLMNOPQRSTUVWXYZ";

unique_ptr<SslContext> ssl_ctx;

#ifndef SOURCE_DIR
#error missing SOURCE_DIR compiler define
#endif

enum Scenarios {
  server_no_accept       = 1 << 0,
  server_redudant_close  = 1 << 1,
  server_multiple_close  = 1 << 2,
  with_clients           = 1 << 3,
  server_write_no_accept = 1 << 4,
  server_high_trasmit    = 1 << 5
};

template<typename T>
struct Client {

  std::unique_ptr<T> sock;
  size_t bytes_sent = 0;
  size_t bytes_recv = 0;
  std::string recv_data;

  explicit Client(std::unique_ptr<T> s)
    : sock(std::move(s)),
      bytes_sent{0},
      bytes_recv{0}
  {
  }

  Client(Client&& rhs) noexcept
    : sock(std::move(rhs.sock)),
      bytes_sent{0},
      bytes_recv{0}
  {
  }

  ~Client() = default;

};

template<typename T>
struct TestServer
{
  explicit TestServer(int flags = 0);
  ~TestServer();

  Reactor* reactor() { return &_reactor; }

  std::mutex _clients_mtx; // initialise early

  int _flags = 0;

  Reactor _reactor;
  std::unique_ptr<T> _server;

  std::list< Client<T> > _clients;

  bool _clients_accepting = true;;

  bool _continue_loop = true;
  std::thread _thread;

  int port = -1;

  void thread_main();
};


void make_tcp_socket(Reactor* reactor, unique_ptr<TcpSocket>& ptr) {
  ptr = make_unique<TcpSocket>(reactor);
}


void make_tcp_socket(Reactor* reactor, unique_ptr<SslSocket>& ptr) {
  ptr = make_unique<SslSocket>(ssl_ctx.get(), reactor);
}

template<typename T>
TestServer<T>::TestServer(int flags)
{
  make_tcp_socket(this->reactor(), _server);

   std::function<void(std::unique_ptr<T>&)> cb = [this, flags](std::unique_ptr<T>& sock) {
    /* io-thread */

    std::lock_guard<std::mutex> guard(_clients_mtx);

    if (!_clients_accepting)
      return;

    // register for read events
    sock->start_read([sock_ptr=sock.get(), flags](char* buf, ssize_t n){
      /* io-thread */

      if (n) {
        sock_ptr->write(buf, n);
      }
      else {
        // printf("server: recv eof at client!\n");
        if (flags & Scenarios::server_redudant_close)
          sock_ptr->close();
        if (flags & Scenarios::server_multiple_close) {
          sock_ptr->close();
          sock_ptr->close();
          sock_ptr->close();
          sock_ptr->close();
          sock_ptr->close();
        }
      }
    });
    Client client(std::move(sock));
    _clients.push_back(std::move(client));
  };

  // if flag are set, override the callack
  if (_flags & Scenarios::server_write_no_accept)
    cb = [](std::unique_ptr<T>& sock){
      for (int i = 0; i <100; i++)
        sock->write("WONT ACCEPT");
    };
  else if (_flags & Scenarios::server_no_accept)
    cb = [](std::unique_ptr<T>&){};

  int starting_port = 5544;

  int port = starting_port;
  bool listening = false;
  for (int i = 0; i < 1000; i++) {
    try {
      _server->listen(port, cb);
      this->port = port;
      listening = true;
      break;
    }
    catch (const std::system_error& e) {
    }
    port++;
  }
  if (!listening)
    throw runtime_error("failed to create a TcpSocket listener");

  //this->port = set_to_listen(starting_port, _server, cb);
  // printf("listening on port %i\n", this->port);

  _thread = std::thread(&TestServer::thread_main, this);
}



template<typename T>
void TestServer<T>::thread_main()
{
  _continue_loop = true;

  if (_flags & Scenarios::server_high_trasmit) {
    while (_continue_loop) {
      std::lock_guard<std::mutex> guard(_clients_mtx);

      for (auto & client : _clients) {
        for (int i = 0; i < 1000 && client.sock; i++)
          if (client.sock->is_open()) {
            auto res = client.sock->write(MSG);
            if (res == T::write_err::success) {
              client.bytes_sent += MSG.size();
            }

            if (res == T::write_err::no_space) {
              LOG_WARN("dropping slow consumer after sending "<<
                       client.bytes_sent <<" bytes");
              client.sock->close();
              client.sock.reset(nullptr);
              break;
            }
          }
      }
    }
  }
}

template<typename T>
TestServer<T>::~TestServer()
{
  {
    // ensure we don't accept any more clients, because there can be a race
    // condition where we have delete the list but the IO thread is about to
    // jump into it.
    std::lock_guard<std::mutex> guard(_clients_mtx);
    _clients_accepting = false;
  }

  _continue_loop = false;
  _thread.join();
}


unique_ptr<TcpSocket> create_connected_tcp(Reactor* reactor,
                                           string addr,
                                           int port,
                                           int timeout_sec=15)
{
  unique_ptr<TcpSocket> sock = make_unique<TcpSocket>(reactor);

  auto prom = make_shared<promise<int>>();
  TcpSocket ::connect_complete_cb_t connected_cb = [&](int err) {
    prom->set_value(err);
  };

  sock->connect(addr, port, timeout_sec, connected_cb);

  // wait for completion
  auto fut = prom->get_future();
  int err = fut.get();

  if (err) {
    cout << "connection failed with error: " << err << endl;
    return nullptr;
  }

  return sock;
}


TEST_CASE("server_and_client_normal") {
  TestServer<TcpSocket> server;
  Reactor reactor;

  size_t msg_count = 200000;
  size_t byte_count = MSG.size() * msg_count;

  int client_count = 10;

  for (int i = 0; i < client_count; i++) {

    auto sock = create_connected_tcp(&reactor, "127.0.0.1", server.port);
    assert(sock);
    Client client{std::move(sock)};

    auto prom = std::make_shared<promise<void>>();

    client.sock->start_read([&](char* buf , ssize_t n) {
      client.bytes_recv += n;
      client.recv_data += std::string_view(buf, n);
      if (client.bytes_recv == byte_count)  {
        cout << "data received " <<  client.bytes_recv << endl;
        prom->set_value();
      }
    });

    for (size_t j = 0; j < msg_count; j++) {
      while (client.sock->write(MSG) == TcpSocket::write_err::no_space)
        sleep(1);
      client.bytes_sent += MSG.size();
    }

    future_status status = prom->get_future().wait_for(15s);
    switch (status) {
      case future_status::ready:
        assert(client.bytes_sent == client.bytes_recv);
        break;
      default:
        ostringstream oss;
        oss << __FUNCTION__ << " future was not ready";
        throw runtime_error(oss.str());
    }
  }

}

int main(int argc, char** argv)
{
  SslConfig ssl_config(true);
  ssl_config.certificate_file = SOURCE_DIR "/src/tests/test-cert.pem";
  ssl_config.private_key_file = SOURCE_DIR "/src/tests/test-private.pem";

  ssl_ctx = std::make_unique<SslContext>(ssl_config);



  try {
    int result = quicktest::run(argc, argv);
    return (result < 0xFF ? result : 0xFF);
  } catch (exception& e) {
    cout << e.what() << endl;
    return 1;
  }
}
