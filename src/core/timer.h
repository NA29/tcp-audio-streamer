#pragma once
#include "core/socket.h"

// A periodic timer that is ALSO a file descriptor (timerfd_create), so the event
// loop waits on the clock the same way it waits on sockets -- one epoll_wait, no
// separate thread, no sleeping.
//
// This is the Linux "everything is a file descriptor" idea paying off: signals
// (signalfd), timers (timerfd) and events (eventfd) all become epoll sources.
// Each expiry makes the fd readable; you must read() the 8-byte expiry count or
// level-triggered epoll will report it ready forever.
class TimerFd {
private:
  Socket fd_; // reuse the RAII wrapper: a timerfd is closed with close()

public:
  static Result<TimerFd> create_periodic(uint32_t period_ms);

  explicit TimerFd(Socket fd);

  int fd() const noexcept { return fd_.fd(); }

  // Consumes the expiry count. Returns how many periods elapsed since the last
  // read -- greater than 1 means we fell behind.
  uint64_t consume_expirations();
};
