#pragma once

#include <string>
#include <utility>

enum class ErrorCode {
  None = 0, // default value when initializing

  // System / OS errors
  SyscallFailed,

  // Networking errors
  SocketCreateFailed,
  SocketBindFailed,
  SocketListenFailed,
  SocketAcceptFailed,
  SocketOptionFailed,
  FcntlFailed,

  // epoll errors
  EpollCreateFailed,
  EpollCtlFailed,
  EpollWaitFailed,

  // I/O errors
  SendFailed,
  RecvFailed,

  // not an error: the non-blocking "nothing available right now" signal (EAGAIN)
  WouldBlock,

  // fallback
  Unknown
};

struct Error {
  ErrorCode code;
  std::string message;

  Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};
