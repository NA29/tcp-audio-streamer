#pragma once
#include "result.h"

class Socket {
private:
  int fd_;

public:
  int fd() const noexcept { return fd_; }

  static Result<Socket> create(); // static bc doesnt need an instance to exist

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
};
