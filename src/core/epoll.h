#pragma once
#include "core/socket.h"
#include <sys/epoll.h>
#include <vector>

// Thin RAII wrapper over the three epoll syscalls.
//
// The key idea: epoll_create1() returns a FILE DESCRIPTOR referring to a kernel-side set of
// sockets we care about. Because it is just an fd, our existing Socket RAII class owns it and
// closes it for us -- no new lifetime code needed.
//
//   create()      -> epoll_create1 : make the set
//   add/mod/del() -> epoll_ctl     : register a socket ONCE; it stays registered
//   wait()        -> epoll_wait    : sleep until something is ready, get back ONLY ready fds
class Epoll {
private:
  Socket epfd_; // the epoll instance is itself an fd, so RAII already covers it
  std::vector<epoll_event> events_;

public:
  static Result<Epoll> create(int max_events = 64);

  explicit Epoll(Socket epfd, int max_events);

  // Watch `fd` for the given event mask (EPOLLIN = readable, EPOLLOUT = writable).
  Status add(int fd, uint32_t events);

  // Change what we watch on an already-registered fd. Used for backpressure later:
  // we switch EPOLLOUT on only while a client has queued bytes waiting to be sent.
  Status mod(int fd, uint32_t events);

  // Stop watching. Note: closing an fd removes it from the set automatically, but being
  // explicit avoids surprises when a descriptor number gets reused.
  Status del(int fd);

  // Blocks until at least one fd is ready (timeout_ms = -1 means "forever").
  // Returns a view over the ready events -- length is however many fired, not how many
  // sockets we are watching. THAT is the whole performance argument for epoll.
  Result<int> wait(int timeout_ms = -1);

  const epoll_event &event_at(int i) const { return events_[static_cast<size_t>(i)]; }
};
