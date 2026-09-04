#include "server/server.h"
#include "utils/log.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>

void Server::run() {
  auto listener_result = Socket::create_listener(kPort, kListenBacklog);
  if (!listener_result) {
    LOG_ERROR("listener setup failed: %s", listener_result.error().message.c_str());
    return;
  }
  Socket listener = std::move(listener_result.value());

  auto epoll_result = Epoll::create(kMaxEvents);
  if (!epoll_result) {
    LOG_ERROR("epoll_create1 failed: %s", epoll_result.error().message.c_str());
    return;
  }
  Epoll epoll = std::move(epoll_result.value());

  // Register the listener. "Readable" on a listening socket means: a connection is
  // waiting to be accepted.
  auto added = epoll.add(listener.fd(), EPOLLIN);
  if (!added) {
    LOG_ERROR("epoll_ctl(listener) failed: %s", added.error().message.c_str());
    return;
  }

  LOG_INFO("listening on port %d (epoll, single thread)", kPort);

  // ------------------------- the event loop -------------------------
  // The ONLY place this thread ever sleeps is epoll_wait. Everything below it must
  // complete quickly and return.
  while (true) {
    auto ready = epoll.wait(-1); // -1 = block indefinitely until something happens
    if (!ready) {
      LOG_ERROR("epoll_wait failed: %s", ready.error().message.c_str());
      break;
    }

    const int n = ready.value();
    for (int i = 0; i < n; ++i) {
      const epoll_event &ev = epoll.event_at(i);
      const int fd = ev.data.fd;

      if (fd == listener.fd()) {
        accept_new_clients(listener, epoll);
        continue;
      }

      // EPOLLHUP: peer hung up. EPOLLERR: socket error. Either way the client is done.
      if (ev.events & (EPOLLHUP | EPOLLERR)) {
        drop_client(fd, epoll, "hangup or socket error");
        continue;
      }

      if (ev.events & EPOLLIN) {
        on_readable(fd, epoll);
      }
    }
  }
}

void Server::accept_new_clients(Socket &listener, Epoll &epoll) {
  // One epoll notification can cover MULTIPLE pending connections, so we must loop.
  // Accepting only one per wakeup would leave clients stuck in the backlog.
  while (true) {
    auto accepted = listener.accept_one();

    if (!accepted) {
      if (accepted.error().code == ErrorCode::WouldBlock) {
        return; // backlog drained -- the normal exit
      }
      LOG_ERROR("accept failed: %s", accepted.error().message.c_str());
      return;
    }

    Socket client = std::move(accepted.value());

    auto nb = client.set_nonblocking();
    if (!nb) {
      LOG_ERROR("set_nonblocking failed: %s", nb.error().message.c_str());
      continue; // client's destructor closes the fd here
    }

    const int fd = client.fd();
    auto added = epoll.add(fd, EPOLLIN);
    if (!added) {
      LOG_ERROR("epoll_ctl(add client) failed: %s", added.error().message.c_str());
      continue;
    }

    clients_.emplace(fd, std::move(client)); // map takes ownership of the socket
    LOG_INFO("client connected (fd=%d, total=%zu)", fd, clients_.size());
  }
}

void Server::on_readable(int fd, Epoll &epoll) {
  char buffer[kRecvBufferSize];

  // Drain what is available. Level-triggered epoll would re-notify us if we stopped
  // early, but draining now saves a trip through epoll_wait.
  while (true) {
    ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);

    if (n > 0) {
      LOG_INFO("fd=%d received %zd bytes", fd, n);
      continue; // there may be more queued
    }

    if (n == 0) {
      drop_client(fd, epoll, "client closed connection");
      return;
    }

    // n == -1
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return; // nothing left right now: hand the thread back to the loop
    }
    if (errno == EINTR) {
      continue; // interrupted by a signal, retry
    }

    drop_client(fd, epoll, std::strerror(errno));
    return;
  }
}

void Server::drop_client(int fd, Epoll &epoll, const char *why) {
  epoll.del(fd);      // stop watching before the fd goes away
  clients_.erase(fd); // erasing destroys the Socket, which closes the fd
  LOG_INFO("client dropped (fd=%d): %s", fd, why);
}
