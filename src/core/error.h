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

  // I/O errors
  SendFailed,
  RecvFailed,

  // fallback
  Unknown
};

struct Error {
  ErrorCode code;
  std::string message;

  Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};
