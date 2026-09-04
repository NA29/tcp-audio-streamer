#pragma once
#include "audio/source.h"
#include "core/epoll.h"
#include "core/socket.h"
#include "core/timer.h"
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

  bool streaming = false; // has this client sent Hello?
  size_t audio_pos = 0;   // per-client cursor, so clients can join at any time

  size_t pending_bytes() const { return send_buffer.size() - send_pos; }
};

class Server {
public:
  // wav_path may be null/missing -- we fall back to a synthesized tone so the
  // server always runs.
  void run(const char *wav_path);

  static constexpr uint16_t kPort = 12345;
  static constexpr int kListenBacklog = 512; // was 5: too small under any real load
  static constexpr size_t kRecvBufferSize = 4096;
  static constexpr int kMaxEvents = 64;

  // Cap on queued-but-unsent bytes per client, expressed in TIME rather than bytes.
  // For a live stream, buffering more than a couple of seconds is pointless: the
  // client is already too far behind to recover, and holding the data only costs
  // us memory. Deriving it from the audio rate keeps the limit meaningful whatever
  // the sample rate is (2 s of 44.1 kHz stereo 16-bit is ~350 KB; of 8 kHz mono
  // it is ~32 KB). A fixed byte cap would mean wildly different amounts of audio.
  static constexpr uint32_t kMaxBufferedMs = 2000;
  static constexpr size_t kMinSendBufferCap = 64 * 1024; // floor for tiny formats
  // Stream cadence. 20 ms is the usual audio trade-off: small enough that latency
  // and jitter stay low, large enough that we are not doing a syscall every sample.
  static constexpr uint32_t kTickMs = 20;

  // Keep the kernel's outbound buffer small so backpressure surfaces in our own
  // queue rather than hiding as latency inside the kernel.
  static constexpr int kSocketSendBuffer = 64 * 1024;

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

  // One timer tick: hand every streaming client its next slice of audio.
  void on_tick(Epoll &epoll, uint64_t missed_ticks);

  // Append bytes to a client's outbound queue and try to flush immediately.
  void queue_send(int fd, ClientSession &session, const std::vector<uint8_t> &bytes,
                  Epoll &epoll);

  // Write as much of the queue as the kernel will take. Returns false if the client
  // was dropped (caller must not touch the session afterwards).
  bool flush_send_buffer(int fd, ClientSession &session, Epoll &epoll);

  // Per-client state now lives here instead of on a blocked call stack.
  // The map owns the Socket, so erasing an entry closes the fd (RAII).
  std::unordered_map<int, ClientSession> clients_;
  audio::AudioSource audio_ = audio::AudioSource::synthetic_tone();

  // How far a client may fall behind before we drop it. Computed once the audio
  // format is known.
  size_t max_send_buffer_ = kMinSendBufferCap;
};
