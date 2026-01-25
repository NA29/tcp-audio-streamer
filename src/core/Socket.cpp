#include "socket.h"
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h> // for close()

Result<Socket> Socket::create() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    return Error{ErrorCode::SocketCreateFailed, std::strerror(errno)};
  }
  return Socket(fd); // temp Socket object -> moved into Result
}

Socket::Socket() : fd_(-1) {}

Socket::Socket(int fd) : fd_(fd) {}

Socket::~Socket() noexcept {
  if (fd_ != -1) {
    ::close(fd_);
    fd_ = -1;
  }
}

// move constructor: creates new object from rvalue reference
Socket::Socket(Socket &&temp_socket) noexcept : fd_(temp_socket.fd_) {
  temp_socket.fd_ = -1;
}

// move assignment operator: replaces current object contents with rvalue
// reference's content
// overload of "operator=" function -> define return and param type
Socket &Socket::operator=(Socket &&temp_socket) noexcept {
  if (this != &temp_socket) {
    if (this->fd_ != -1) {
      ::close(this->fd_);
    }
    this->fd_ = temp_socket.fd_;
    temp_socket.fd_ = -1;
  }

  return *this;
}
