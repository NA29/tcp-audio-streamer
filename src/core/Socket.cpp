#include "socket.h"
#include "utils/log.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h> // for close()

#include <cerrno>  // errno
#include <cstring> // std::strerror

Result<Socket> Socket::create() {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0); // == ipv4, tcp, default protocol
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

Status Socket::set_nonblocking() {
  // read the current flags, OR in O_NONBLOCK, write them back.
  // We must not clobber the other flags, hence the read-modify-write.
  int flags = ::fcntl(fd_, F_GETFL, 0);
  if (flags == -1) {
    return Error{ErrorCode::FcntlFailed, std::strerror(errno)};
  }
  if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
    return Error{ErrorCode::FcntlFailed, std::strerror(errno)};
  }
  return Unit{};
}

Status Socket::set_send_buffer(int bytes) {
  // Linux autotunes SO_SNDBUF up to a couple of MB. For a live stream that is
  // actively harmful: it silently queues seconds of stale audio inside the kernel,
  // where we cannot see it, cannot age it out, and cannot apply our own policy.
  // Capping it keeps the queue in OUR buffer, where send() reports EAGAIN promptly
  // and the slow-consumer rule can act on real numbers.
  // (Note: the kernel stores double what you ask for -- it reserves half for
  // bookkeeping -- so getsockopt reports 2x this value.)
  if (::setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == -1) {
    return Error{ErrorCode::SocketOptionFailed, std::strerror(errno)};
  }
  return Unit{};
}

Result<Socket> Socket::accept_one() {
  int client_fd = ::accept(fd_, nullptr, nullptr);

  if (client_fd == -1) {
    // EAGAIN/EWOULDBLOCK: the backlog is drained. Expected, not an error.
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return Error{ErrorCode::WouldBlock, "no pending connections"};
    }
    // ECONNABORTED: peer disappeared between the handshake and our accept(). Skip it.
    // EINTR: a signal interrupted the call. Retry next loop iteration.
    if (errno == ECONNABORTED || errno == EINTR) {
      return Error{ErrorCode::WouldBlock, std::strerror(errno)};
    }
    return Error{ErrorCode::SocketAcceptFailed, std::strerror(errno)};
  }

  return Socket(client_fd);
}

Result<Socket> Socket::create_listener(uint16_t port, int backlog) {
  auto socket_result = Socket::create();
  if (!socket_result) {
    return socket_result.error();
  }
  Socket listener = std::move(socket_result.value());

  // Without SO_REUSEADDR, restarting the server fails with EADDRINUSE for ~60s while the
  // old socket sits in TIME_WAIT. You WILL hit this within five minutes otherwise.
  const int one = 1;
  if (::setsockopt(listener.fd(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
    return Error{ErrorCode::SocketOptionFailed, std::strerror(errno)};
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port); // host -> network byte order

  if (::bind(listener.fd(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    return Error{ErrorCode::SocketBindFailed, std::strerror(errno)};
  }

  if (::listen(listener.fd(), backlog) == -1) {
    return Error{ErrorCode::SocketListenFailed, std::strerror(errno)};
  }

  auto nb = listener.set_nonblocking();
  if (!nb) {
    return nb.error();
  }

  return listener;
}
