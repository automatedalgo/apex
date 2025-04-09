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

#include <string>
#include <list>

using namespace apex;
using namespace std;

int main() {
  try {
    string host = "0.0.0.0";
    string port = "55533";

    Reactor reactor;

    list<unique_ptr<TcpSocket>> clients;

    TcpSocket::on_accept_cb_t on_accept = [&](unique_ptr<TcpSocket>& sock) {
      cout << "connection received" << endl;

      sock->start_read([sockptr=sock.get()](char* buf, ssize_t n) {
        if (n>0) {
          cout << string(buf, n);
          sockptr->write(buf, n);
        }
        else
          sockptr->close();
      });
      clients.push_back(move(sock));
    };

    auto sock = make_unique<TcpSocket>(&reactor);
    sock->listen(host, port, on_accept);

    cout << "listening on " << host << ":" << port <<  endl;

    sleep(60);
    return 0;
  }
  catch (const std::exception& err) {
    cerr << err.what() << endl;
    return 1;
  }
}
