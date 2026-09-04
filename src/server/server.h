#pragma once
#include "core/epoll.h"
#include "core/socket.h"
#include "proto/parser.h"

#include <cstdint>
#include <unordered_map>

// Everything we remember about one connected client. In the blocking version this
// state lived implicitly on handle_client()'s stack; with an event loop the stack
// unwinds after every read, so it has to live here.
struct ClientSession {
  Socket socket;
  proto::FrameParser parser; // where we are mid-message
};

class Server {
public:
  void run();

  static constexpr uint16_t kPort = 12345;
  static constexpr int kListenBacklog = 512; // was 5: too small under any real load
  static constexpr size_t kRecvBufferSize = 4096;
  static constexpr int kMaxEvents = 64;

private:
  // Drain the accept backlog. Non-blocking, so we loop until EAGAIN.
  void accept_new_clients(Socket &listener, Epoll &epoll);

  // One client has readable bytes. Read what is there, then RETURN -- never loop
  // waiting for more, or we stall every other client.
  void on_readable(int fd, Epoll &epoll);

  void drop_client(int fd, Epoll &epoll, const char *why);

  // Handle one fully-parsed application message.
  void on_frame(int fd, const proto::Frame &frame);

  // Per-client state now lives here instead of on a blocked call stack.
  // The map owns the Socket, so erasing an entry closes the fd (RAII).
  std::unordered_map<int, ClientSession> clients_;
};
