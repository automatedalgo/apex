#include <apex/infra/Reactor.hpp>

#include <assert.h>
#include <sys/types.h>

namespace apex {

std::string err_to_string(int e)
{
  std::string retval;

  char errbuf[256] = {0};

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

  return "unknown errno";
}


void eventstr(std::ostream& os, int e)
{
  const char* delim="";
  if (e bitand POLLIN)    { os << delim << "POLLIN"; delim="|"; }   // seen
  if (e bitand POLLPRI)   { os << delim << "POLLPRI"; delim="|"; }
  if (e bitand POLLOUT)   { os << delim << "POLLOUT"; delim="|"; }  // seen
  if (e bitand POLLRDHUP) { os << delim << "POLLRDHUP"; delim="|"; }
  if (e bitand POLLERR)   { os << delim << "POLLERR"; delim="|"; }
  if (e bitand POLLHUP)   { os << delim << "POLLHUP"; delim="|"; }
  if (e bitand POLLNVAL)  { os << delim << "POLLNVAL"; delim="|"; }  // seen
};


Stream::~Stream() {
}


Reactor::Reactor()
  : _pipefd{-1}
{
  // create the interrupt pipe
  _pipefd[0] = -1;  // pipefd[0] refers to the read end of the pipe
  _pipefd[1] = -1;  // pipefd[1] refers to the
  if (::pipe(_pipefd) < 0)
    assert_perror(errno);

  // set internal pipe to non-blocking
  for (size_t i = 0; i < sizeof(_pipefd)/sizeof(_pipefd[0]); i++) {
    int fl = ::fcntl(_pipefd[i], F_GETFL);
    if (fl < 0)
      assert_perror(errno);
    if (::fcntl(_pipefd[i], F_SETFL, fl | O_NONBLOCK) < 0)
      assert_perror(errno);
  }

  // finally start the reactor thread
  _thread = std::thread(&Reactor::thread_main, this);

  // wait until we have a thread ID
  while (_thread_id == std::thread::id{})
    usleep(1000);
}


Reactor::~Reactor()
{
  // signal reactor thread to exit
  push_command({Command::Type::exit, nullptr});
  _thread.join();
  ::close(_pipefd[0]);
  ::close(_pipefd[1]);
}


bool Reactor::is_reactor_thread() const
{
  return _thread_id == std::this_thread::get_id();
}


void Reactor::push_command(Command cmd)
{
  {
    std::lock_guard<std::mutex> guard(_commands_mtx);
    _commands.push(cmd);
  }
  char c = 'x';
  if (::write(_pipefd[1], &c, 1) < 0)  // TODO: EINTR
    assert_perror(errno);
}


void Reactor::stream_user_cb(Stream* stream) {
  push_command({Command::Type::user_cb, stream});
}


void Reactor::add_stream(Stream* stream)
{
  push_command({Command::Type::add, stream});
}


void Reactor::detach_stream(Stream* stream)
{
  if (is_reactor_thread())
  {
    // printf("immediate IO detach for stream %d\n", stream->fd);
    stream->disposing = true;
    push_command({Command::Type::dispose, stream});
  }
  else {
    // printf("synchronous IO detach for stream %d\n", stream->fd);
    auto promise = std::make_shared<std::promise<void>>();
    auto cb = [&promise]() {
      promise->set_value();
    };
    stream->on_dispose_cb = cb;
    push_command({Command::Type::dispose, stream});
    promise->get_future().wait();
  }
}


void Reactor::start_read(Stream* stream)
{
  push_command({Command::Type::start_read, stream});
}


void Reactor::start_accept(Stream* stream)
{
  push_command({Command::Type::start_accept, stream});
}

void Reactor::start_write(Stream* stream)
{
  push_command({Command::Type::start_write, stream});
}


void Reactor::close_stream(Stream* stream)
{
  push_command({Command::Type::close, stream});
}


pollfd make_pollfd(int fd, int events) {
  pollfd pfd;
  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = fd;
  pfd.events = events;
  return pfd;
}


void Reactor::reactor_main_loop()
{
  bool continue_loop = true;
  std::vector<pollfd> fds;
  std::vector<Stream*> streams;
  char buf[10240] = {0};
  bool have_timers;

  while (continue_loop) {
    fds.clear();
    streams.clear();
    have_timers = false;

    // add interrupt internal pipe
    fds.push_back(make_pollfd(_pipefd[0], POLLIN));
    streams.push_back(nullptr);

    // TODO: it would be better to deduce whether POLLIN / POLLOUT are required,
    // based on various interestedIn flags, rather than turning these bits on
    // and over from various places in the code.


    // build the array of pollfd, and companion array of Stream*
    for (size_t i = 0; i < _streams.size(); i++) {
      Stream* stream = _streams[i];
      if (stream && stream->has_fd() && !stream->err && !stream->eof) {
        have_timers |= (stream->timeout >0) ;
        int events = stream->events;
        if (stream->hangup)
          events = events & (~POLLOUT);
        fds.push_back(make_pollfd(stream->fd, events));
        streams.push_back(stream);
      }
    }

    int timeout = (have_timers)? 1000: -1;

#if 0
    {
      std::ostringstream osout;
      for (std::vector<pollfd>::const_iterator i = fds.begin();
           i != fds.end(); ++i)
      {
        osout << "[" << "fd=" << i->fd << ", events=" << i->events << "(";
        eventstr(osout, i->events);
        osout << "),]";
      }
      std::cout << "reactor: into poll, #fds: " << fds.size() << " events: " << osout.str() << "\n";
    }
#endif

    int nfds = ::poll(&fds[0], fds.size(), timeout);

    if (nfds == -1 && errno != EINTR)
      abort(); // TODO: add msg & errostr here

#if 0
    {
      std::ostringstream osout;
      osout << "nfds=" << nfds << ", ";
      for (std::vector<pollfd>::const_iterator i = fds.begin();
           i != fds.end(); ++i)
      {
        if (i->revents)
        {
          osout << "[";
          osout << "fd=" << i->fd << ", revents=" << i->revents << "(";
          eventstr(osout, i->revents);
          osout << "),";
          osout << "]";
        }
      }
      std::cout << "reactor: from poll, revents: " << osout.str() << "\n";
    }
#endif

    while (nfds > 0) {
      nfds -= !!fds[0].revents;
      for (size_t i = 1; i < std::size(fds) ; i++) // fds[0] handled later
      {
        Stream* const s = streams[i];
        int const revents = fds[i].revents;
        bool has_err = false;

        nfds -= !!revents;

        if (revents & POLLOUT)
        {
          auto n = 0;
          if (!s->disposing)
            n = s->on_write_cb();
          if (n == 0)
            s->events &= ~POLLOUT;  // all sent, clear POLLOUT bit
          has_err |= (n < 0); // write error
        }

        if (revents & POLLIN)
        {
          if (s->on_read_cb) { // want read
            int nread;
            do {
              nread = ::read(s->fd, buf, sizeof(buf));
            } while (nread == -1 && errno == EINTR);

            s->eof |= (nread == 0);

            if (nread >= 0) {
              /* read success */
              if (!s->disposing)
                s->on_read_cb(buf, nread);
            }
            else {
              /* read error */
              if (errno != EAGAIN && errno != EWOULDBLOCK) {
                s->read_err = errno;
                has_err |= 1;
              }
            }
          }
          else if (s->on_connection_cb)  { // want accept

            // sockaddr_in addr;
            // socklen_t addr_len = sizeof(addr);
            // memset(&addr, 0, sizeof(addr));
            // int accept_fd = ::accept4(s->fd, (sockaddr*)&addr, &addr_len, SOCK_NONBLOCK);

            // if (!s->disposing)
            //   s->on_accept_cb(accept_fd, &addr);
            // else {
            //   ::close(accept_fd);
            // }

            if (!s->disposing)
              s->on_connection_cb(s, 0);
          }

        }

        has_err |= (revents & (POLLERR|POLLNVAL));

        // POLLHUP indicates the peer closed its end of the channel, however
        // subsequent reads might be successful until read returns 0.
        s->hangup |= (revents & (POLLHUP));
        s->err |= has_err;

        if (has_err && !s->disposing) {
          // decide an error code
          int ec = std::max(std::max(s->read_err, s->write_err), 1);
          s->on_read_cb(buf, -ec);
        }
      }
    } // end of polling

    // process any timeouts
    if (have_timers) {
      time_t now = time(nullptr);
      for (size_t i = 0; i < _streams.size(); i++) {
        Stream* s = _streams[i];
        if (s && s->has_fd() && s->timeout >= 0 && s->timeout < now) {
          s->events &= ~POLLOUT;  // stop polling for connected events
          s->timeout = -1; // stop timer
          if (s->on_connect_timeout_cb && !s->disposing)
            s->on_connect_timeout_cb(s, -1);
        }
      }
    }

    // process any poll interruption events
    if (fds[0].revents) {
      assert(!(fds[0].revents & (POLLNVAL|POLLERR)));
      // drain all bytes in the interruption stream
      while (1) {
        int r = ::read(_pipefd[0], buf, sizeof(buf));
        if (r == sizeof(buf))
          continue;
        if (r != -1)
          break;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        if (errno == EINTR)
          continue;
        abort();
      }

      std::queue<Command> q; // copy the command queue
      {
        std::lock_guard<std::mutex> guard( _commands_mtx);
        q.swap(_commands);
      }

      while (!q.empty()) {
        auto cmd = std::move(q.front());
        q.pop();

        // Note: even if a stream is disposing, we still need to process any
        // such commands. An important command not to miss would be the addition
        // of a stream to the reactor.

        switch (cmd.type) {
          case Command::Type::dispose :
            cmd.stream->disposing = true;
            cmd.stream->do_delete = true;
            break;
          case Command::Type::exit :
            continue_loop = false;
            break;
          case Command::Type::none :
            break;
          case Command::Type::add :
            // printf("reactor: adding stream %i\n", cmd.stream->fd);
            for (size_t i = 0; i < _streams.size() && !!cmd.stream; i++)
              if (_streams[i] == nullptr)
                std::swap(_streams[i], cmd.stream);
            if (cmd.stream)
              _streams.push_back(cmd.stream);
            break;
          case Command::Type::start_read:
            cmd.stream->events |= POLLIN;
            break;
          case Command::Type::start_write :
            cmd.stream->events |= POLLOUT;
            assert(cmd.stream->on_write_cb);
            break;
          case Command::Type::close :
            cmd.stream->do_close = true;
            break;
          case Command::Type::user_cb :
            if (!cmd.stream->disposing)
              cmd.stream->user_cb();
            break;
          case Command::Type::start_accept:
            cmd.stream->events |= POLLIN;
            break;
        }
      }
    } // end of cmd processing

    // handle sockets that need close
    for (auto s: _streams) {
      if (s && s->has_fd() && (s->do_close|s->do_delete)) {
        // printf("reactor: closing fd:%i\n", s->fd);
        while (::close(s->fd) < 0)
          if (errno != EINTR)
            break;
        s->fd = NULL_FD;
      }
    }


    // handle streams needing delete
    for (auto & s : _streams) { // need Stream& ref to reset pointer
      if (s && s->do_delete) {
        // this callback doesn't need disposing check, because if disposing flag
        // was set on the IO thread the callback won't be set, otherwise if has
        // been set it means a caller is waiting.
        if (s->on_dispose_cb)
          s->on_dispose_cb();
        // printf("reactor: deleting stream fd:%i\n", s->fd);
        delete s;
        s = nullptr;
      }
    }
  } // main while loop

  // close any remaining sockets
  for (auto s : _streams) {
    if (s && s->has_fd()) {
      // printf("::close(%i)\n", s->fd);
      while (::close(s->fd) < 0)
        if (errno != EINTR)
          break;
    }
    delete s;
  }
}


/* Thread entry point */
void Reactor::thread_main() {
  _thread_id = std::this_thread::get_id();
  reactor_main_loop();
}

}
