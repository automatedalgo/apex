#include "TcpSocket.hpp"
#include "TcpConnector.hpp"
// #include "utils.hpp"

#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define LOG_INFO( X ) do { std::cout << "info: " << X << "\n";} while (0);
#define LOG_WARN( X ) do { std::cout << "warning: "<< X << "\n";} while (0);


namespace {

int set_socket_non_blocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0)
    return -1;
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;
  // printf("fd=%i: set to non-blocking\n", fd);
  return 0;
}

void abort_with_msg(const char* msg, const char* reason) {
  fprintf(stderr, "%s: %s\n", msg, reason);
  abort();
}

std::string errno_str(int e)
{
  std::string retval;

  char errbuf[256];
  memset(errbuf, 0, sizeof(errbuf));

/*

TODO: here I should be using the proper feature tests for the XSI
implementation of strerror_r .  See man page.

  (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE

*/

#ifdef _GNU_SOURCE
  // the GNU implementation might not write to errbuf, so instead, always use
  // the return value.
  return ::strerror_r(e, errbuf, sizeof(errbuf)-1);
#else
  // XSI implementation
  if (::strerror_r(e, errbuf, sizeof(errbuf)-1) == 0)
    return errbuf;
#endif

  return "unknown";
}

}
namespace apex {

TcpConnector::TcpConnector(Reactor* reactor, completed_cb_t cb) :
  _reactor(reactor),
  _completed_cb(std::move(cb)),
  _timeout_sec(0),
  _addrs(nullptr),
  _next(nullptr),
  _last_errno(0),
  _completed(false)
{
  assert(_completed_cb);

  _timer_stream = std::make_unique<Stream>(NULL_FD);
  _timer_stream->user_cb = [this](){try_next_addr();};
  _reactor->add_stream(_timer_stream.get());
}


TcpConnector::~TcpConnector()
{
  if (_addrs)
    ::freeaddrinfo(_addrs);

  _reactor->detach_stream_unique_ptr(_timer_stream);
  _reactor->detach_stream_unique_ptr(_stream);
}


bool TcpConnector::is_completed() const
{
  return _completed;
}


void TcpConnector::connect(std::string addr, std::string service,
                           int timeout_sec)
{
  if (addr.empty() && service.empty())
    throw std::runtime_error("cannot TCP connect() for empty addr & service");

// socket family, AF_INET (ipv4) or AF_INET6 (ipv6), must match host_ip above
  int ip_family = AF_INET;  // TODO: take as an input parameter

  bool resolve_addr = true;

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM; /* Connection based socket */
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_family = ip_family;
  hints.ai_socktype = resolve_addr ? 0 : (AI_NUMERICHOST | AI_NUMERICSERV);

  int err;
  err = ::getaddrinfo(addr.empty()? nullptr: addr.c_str(),
                      service.empty()? nullptr: service.c_str(),
                      &hints, &_addrs);

  // TODO: throw exception - need system excewption or something, and, need to
  // do this for other use of getaddrinfo
  // TODO: will need an excetion that handles getaddrinfo errors
  if (err != 0) {
    abort_with_msg("getaddrinfo", gai_strerror(err));
  }

  _next = _addrs;
  _timeout_sec = time(nullptr) + timeout_sec;

  _reactor->stream_user_cb(_timer_stream.get());
}


void TcpConnector::connect(std::string addr, int port, int timeout_sec)
{
  this->connect(addr, std::to_string(port), timeout_sec);
}


void TcpConnector::try_next_addr()
{
  /* io-thread */

  /* Find next valid address to try, initiate socket connect, which either
   * completes immediately or completes later on the IO loop, bounded by a
   * timeout.
   */

  assert(_reactor->is_reactor_thread());

  int connected_fd = -1;
  int inprogress_fd = -1;

  if (_next == NULL)
    goto no_more_addresses;

  while (_next != NULL && connected_fd==-1 && inprogress_fd==-1) {
    int err = 0;
    auto addr = _next;
    _next = _next->ai_next;

    int fd = ::socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    if (fd == -1)
      continue;  // try next address

    if (set_socket_non_blocking(fd) < 0) {
      ::close(fd);
      continue;  // try next address
    }

    // attempt non-blocking connect
    do {
      err = ::connect(fd, addr->ai_addr, addr->ai_addrlen);
    } while (err == -1 && errno == EINTR);
    // printf("fd=%i: connecting, err %i, errno %i\n", fd, err, errno);

    if (err == 0)
      connected_fd = fd;
    else {
      _last_errno = errno;
      if (errno == EINPROGRESS)
        inprogress_fd = fd; // connect in progress, poll for success/timeout
      else {
        // LOG_WARN("connect failed, errno " << errno);
      }
    }
  } // while loop

  if (connected_fd != -1) {
    _completed_cb(connected_fd, 0);
    _completed = true;
    return;
  }

  if (inprogress_fd != 1) {
    // connect did not complete, but might complete sometime later, so we need
    // to start polling for POLLIN events
    _stream = std::make_unique<TcpStream>(inprogress_fd);
    _stream->events |= POLLOUT;
    _stream->timeout = _timeout_sec;
    _stream->on_connect_timeout_cb = [this](Stream* s, int err) {
      assert(s == _stream.get());
      if (err == -1) {
        _last_errno = ETIMEDOUT;
        _reactor->stream_user_cb(_timer_stream.get()); // schedule next attempt
        _reactor->detach_stream_unique_ptr(_stream);
      }
    };
    // TODO: not sure this is the correct callback function to use ?
    _stream->on_write_cb = [this]() -> ssize_t {
      /* io-thread */
      int const fd = this->_stream->fd;
      int so_error;
      socklen_t len = sizeof(so_error);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0)
        assert_perror(errno); // TODO: replace with die()/throw/log

      if (so_error == 0) {
        // socket connected
        this->_stream->timeout = -1; // disable the timeout

        this->_stream->fd = -1; // we are moving FD into socket object

        // dispose this stream we were using, fd moving to socket object
        _reactor->detach_stream_unique_ptr(_stream);

        _completed_cb(fd, 0);

        _completed = true;
        return 0; // stops further POLLOUT
      }
      else {
        LOG_WARN("connect failed: " << errno_str(so_error));
        this->_last_errno = so_error;
        _reactor->detach_stream_unique_ptr(_stream);
        _reactor->stream_user_cb(_timer_stream.get()); // sched. next attempt
      }
      return -1; /* indicate socket is in error, so will be closed */
    };
    _reactor->add_stream(_stream.get());
    return;
  }

no_more_addresses:
  // no more address to try, connect operation has completed with failure
  _completed_cb(NULL_FD, _last_errno);
  _completed = true;
}

}
