#pragma once
#include "result.h"
#include <cstdint>

class Socket {
private:
  int fd_;

public:
  int fd() const noexcept { return fd_; }
  bool valid() const noexcept { return fd_ != -1; }

  static Result<Socket> create(); // static bc doesnt need an instance to exist

  // Full listening socket setup: socket + SO_REUSEADDR + bind + listen + O_NONBLOCK.
  static Result<Socket> create_listener(uint16_t port, int backlog);

  Socket();

  // otherwise any int can become a Socket implicitly (UB)
  // ex: handleTask(Sockets) will not be flagged by compiler
  explicit Socket(int fd);

  ~Socket() noexcept;

  // important for RAII!!, prevents two objects from accessing the same resource
  // copy constructor : creating a socker from another one (ref)
  Socket(const Socket &) = delete;

  // copy assignment operator (overload)
  Socket &operator=(const Socket &) = delete;

  // move constructor
  Socket(Socket &&temp_socket) noexcept;

  // move assignment operator (overload)
  Socket &operator=(Socket &&temp_socket) noexcept;

  // Switches the fd to non-blocking mode: recv/send/accept return EAGAIN instead of
  // parking the thread. This is the single call that makes an event loop possible.
  Status set_nonblocking();

  // Bounds the kernel's own outbound buffer for this socket.
  Status set_send_buffer(int bytes);

  // Accepts one pending connection. On a non-blocking listener, "nothing left to accept"
  // comes back as ErrorCode::WouldBlock -- that is the normal loop terminator, not a failure.
  Result<Socket> accept_one();
};
