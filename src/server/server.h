#pragma once
#include "core/epoll.h"
#include "core/socket.h"

#include <cstdint>
#include <unordered_map>

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

  // Per-client state now lives here instead of on a blocked call stack.
  // The map owns the Socket, so erasing an entry closes the fd (RAII).
  std::unordered_map<int, Socket> clients_;
};
