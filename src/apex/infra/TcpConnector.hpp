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

  void try_next_addr();

  int last_errno() const { return _last_errno; }

private:
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
