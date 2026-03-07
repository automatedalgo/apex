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

#include "Reactor.hpp"

struct addrinfo;

namespace apex {

class TcpConnector {

public:
  using completed_cb_t = std::function<void(int fd, int err)>;

  TcpConnector(Reactor*, completed_cb_t);
  ~TcpConnector();

  void connect(std::string addr, std::string service, int timeout_sec);
  void connect(std::string addr, int port, int timeout_sec);

  bool is_completed() const;

  int last_errno() const { return _last_errno; }

private:
  void try_next_addr();

  Reactor * _reactor;
  completed_cb_t _completed_cb;
  int _timeout_sec;
  struct ::addrinfo * _addrs;
  struct ::addrinfo * _next;
  std::unique_ptr<Stream> _timer_stream;
  std::unique_ptr<TcpStream> _stream;
  int _last_errno;
  bool _completed;
};

}
