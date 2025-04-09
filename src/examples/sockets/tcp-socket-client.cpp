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

#include <apex/infra/Reactor.hpp>
#include <apex/infra/TcpSocket.hpp>

using namespace apex;
using namespace std;

/*
  Example of using a Reactor and TcpSocket to implement a basic telnet-like
  utility.
 */
int main()
{
  try {
    string host = "127.0.0.1";
    int port = 55533;

    Reactor reactor;

    auto sock = make_unique<TcpSocket>(&reactor);

    auto promise = std::make_shared<std::promise<int>>();
    TcpSocket::connect_complete_cb_t connected_cb = [&](int err) {
      promise->set_value(err);
    };

    cout << "connecting to " << host << ":" << port <<  endl;
    int timeout_secs = 10;
    sock->connect(host, port, timeout_secs, connected_cb);

    // wait for completion
    auto fut = promise->get_future();

    if (!sock->is_open()) {
      cout << "failed to open" << endl;
      return 1;
    }

    cout << "connected" << endl;

    bool continue_loop = true;
    sock->start_read([&](char* buf, ssize_t n) {
      if (n>0) {
        cout << string(buf, n) << endl;
        sock->write("ECHO>");
        sock->write(buf, n);
        if (strncmp(buf, "BYE", 3) == 0)
          continue_loop = false;
      }
      else
        exit(0);
    });

    sock->write("HELLO>\n");
    sock->write("SEND 'BYE' TO EXIT>\n");
    while (continue_loop) {
      struct pollfd fds[1] = {};
      fds[0].fd = STDIN_FILENO;
      fds[0].events = POLLIN;

      int nready = poll(fds, 1, 250);

      if (nready == -1)
        perror("poll");
      if (nready == 0)
        continue; // timeout
      if (nready == 1 && fds[0].revents & POLLIN) {
        char buf[128] = {0};
        int n = read(STDIN_FILENO, buf, sizeof(buf));
        if (strncmp(buf, "BYE", 3) == 0)
          continue_loop = false;
        if (n>0)
          sock->write(buf, n);
      }
    }
    return 0;
  }
  catch (const std::exception& err) {
    cerr << err.what() << endl;
    return 1;
  }
}
