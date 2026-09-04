#include "core/timer.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

TimerFd::TimerFd(Socket fd) : fd_(std::move(fd)) {}

Result<TimerFd> TimerFd::create_periodic(uint32_t period_ms) {
  // CLOCK_MONOTONIC, not CLOCK_REALTIME: monotonic never jumps backwards when the
  // system clock is adjusted (NTP, DST), so the stream cadence cannot hiccup.
  int raw = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (raw == -1) {
    return Error{ErrorCode::TimerCreateFailed, std::strerror(errno)};
  }
  Socket owned{raw};

  itimerspec spec{};
  spec.it_value.tv_sec = period_ms / 1000;           // first expiry
  spec.it_value.tv_nsec = (period_ms % 1000) * 1000000L;
  spec.it_interval = spec.it_value;                  // and every period after

  if (::timerfd_settime(owned.fd(), 0, &spec, nullptr) == -1) {
    return Error{ErrorCode::TimerSetFailed, std::strerror(errno)};
  }
  return TimerFd(std::move(owned));
}

uint64_t TimerFd::consume_expirations() {
  uint64_t ticks = 0;
  ssize_t n = ::read(fd_.fd(), &ticks, sizeof(ticks));
  if (n != sizeof(ticks)) {
    return 0;
  }
  return ticks;
}
