#include "core/epoll.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

Epoll::Epoll(Socket epfd, int max_events)
    : epfd_(std::move(epfd)), events_(static_cast<size_t>(max_events)) {}

Result<Epoll> Epoll::create(int max_events) {
  // EPOLL_CLOEXEC: if we ever fork+exec, the child does not inherit this descriptor.
  int fd = ::epoll_create1(EPOLL_CLOEXEC);
  if (fd == -1) {
    return Error{ErrorCode::EpollCreateFailed, std::strerror(errno)};
  }
  return Epoll(Socket(fd), max_events);
}

Status Epoll::add(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd; // this is what epoll_wait hands back to identify the socket
  if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_ADD, fd, &ev) == -1) {
    return Error{ErrorCode::EpollCtlFailed, std::strerror(errno)};
  }
  return Unit{};
}

Status Epoll::mod(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_MOD, fd, &ev) == -1) {
    return Error{ErrorCode::EpollCtlFailed, std::strerror(errno)};
  }
  return Unit{};
}

Status Epoll::del(int fd) {
  if (::epoll_ctl(epfd_.fd(), EPOLL_CTL_DEL, fd, nullptr) == -1) {
    return Error{ErrorCode::EpollCtlFailed, std::strerror(errno)};
  }
  return Unit{};
}

Result<int> Epoll::wait(int timeout_ms) {
  int n = ::epoll_wait(epfd_.fd(), events_.data(),
                       static_cast<int>(events_.size()), timeout_ms);
  if (n == -1) {
    if (errno == EINTR) {
      return 0; // interrupted by a signal: not an error, just zero ready events
    }
    return Error{ErrorCode::EpollWaitFailed, std::strerror(errno)};
  }
  return n;
}
