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
  proto::FrameParser parser; // where we are mid-message (inbound)

  // Outbound queue. send() on a non-blocking socket writes only as much as the
  // kernel's socket buffer has room for, then returns EAGAIN. We cannot wait --
  // that would stall every other client -- so the unsent remainder lives here.
  std::vector<uint8_t> send_buffer;
  size_t send_pos = 0;        // how much of send_buffer has already gone out
  bool watching_write = false; // is EPOLLOUT currently armed for this fd?

  size_t pending_bytes() const { return send_buffer.size() - send_pos; }
};

class Server {
public:
  void run();

  static constexpr uint16_t kPort = 12345;
  static constexpr int kListenBacklog = 512; // was 5: too small under any real load
  static constexpr size_t kRecvBufferSize = 4096;
  static constexpr int kMaxEvents = 64;

  // Hard cap on queued-but-unsent bytes per client. A consumer that reads slower
  // than we produce would otherwise grow this without bound until the server OOMs.
  // Bounded memory is the entire point: we drop the one slow client instead.
  static constexpr size_t kMaxSendBuffer = 4u << 20; // 4 MiB
  static constexpr size_t kAudioChunkSize = 32 * 1024;
  static constexpr int kBurstChunks = 256; // 8 MiB burst -> guarantees EAGAIN

private:
  // Drain the accept backlog. Non-blocking, so we loop until EAGAIN.
  void accept_new_clients(Socket &listener, Epoll &epoll);

  // One client has readable bytes. Read what is there, then RETURN -- never loop
  // waiting for more, or we stall every other client.
  void on_readable(int fd, Epoll &epoll);

  void drop_client(int fd, Epoll &epoll, const char *why);

  // Handle one fully-parsed application message.
  void on_frame(int fd, const proto::Frame &frame, Epoll &epoll);

  // The socket has room again: push out whatever is queued.
  void on_writable(int fd, Epoll &epoll);

  // Append bytes to a client's outbound queue and try to flush immediately.
  void queue_send(int fd, ClientSession &session, const std::vector<uint8_t> &bytes,
                  Epoll &epoll);

  // Write as much of the queue as the kernel will take. Returns false if the client
  // was dropped (caller must not touch the session afterwards).
  bool flush_send_buffer(int fd, ClientSession &session, Epoll &epoll);

  // Per-client state now lives here instead of on a blocked call stack.
  // The map owns the Socket, so erasing an entry closes the fd (RAII).
  std::unordered_map<int, ClientSession> clients_;
};
