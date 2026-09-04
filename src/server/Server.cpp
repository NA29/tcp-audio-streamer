#include "server/server.h"
#include "utils/log.h"

#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

void Server::run(const char *wav_path) {
  if (wav_path != nullptr) {
    auto loaded = audio::AudioSource::from_wav(wav_path);
    if (!loaded) {
      LOG_ERROR("could not load %s: %s -- falling back to synthetic tone", wav_path,
                loaded.error().message.c_str());
    } else {
      audio_ = std::move(loaded.value());
    }
  }
  max_send_buffer_ = std::max(kMinSendBufferCap, audio_.bytes_for_ms(kMaxBufferedMs));

  LOG_INFO("audio: %u Hz, %u ch, %u-bit, %zu bytes (%.1fs)", audio_.format().sample_rate,
           audio_.format().channels, audio_.format().bits_per_sample,
           audio_.total_bytes(),
           static_cast<double>(audio_.total_bytes()) / audio_.format().bytes_per_second());

  auto listener_result = Socket::create_listener(kPort, kListenBacklog);
  if (!listener_result) {
    LOG_ERROR("listener setup failed: %s", listener_result.error().message.c_str());
    return;
  }
  Socket listener = std::move(listener_result.value());

  auto epoll_result = Epoll::create(kMaxEvents);
  if (!epoll_result) {
    LOG_ERROR("epoll_create1 failed: %s", epoll_result.error().message.c_str());
    return;
  }
  Epoll epoll = std::move(epoll_result.value());

  // Register the listener. "Readable" on a listening socket means: a connection is
  // waiting to be accepted.
  auto added = epoll.add(listener.fd(), EPOLLIN);
  if (!added) {
    LOG_ERROR("epoll_ctl(listener) failed: %s", added.error().message.c_str());
    return;
  }

  // The clock is just another fd in the same epoll set -- no timer thread, and no
  // epoll_wait timeout arithmetic to get wrong.
  auto timer_result = TimerFd::create_periodic(kTickMs);
  if (!timer_result) {
    LOG_ERROR("timerfd failed: %s", timer_result.error().message.c_str());
    return;
  }
  TimerFd timer = std::move(timer_result.value());

  auto timer_added = epoll.add(timer.fd(), EPOLLIN);
  if (!timer_added) {
    LOG_ERROR("epoll_ctl(timer) failed: %s", timer_added.error().message.c_str());
    return;
  }

  LOG_INFO("listening on port %d (epoll, single thread, %u ms tick)", kPort, kTickMs);

  // ------------------------- the event loop -------------------------
  // The ONLY place this thread ever sleeps is epoll_wait. Everything below it must
  // complete quickly and return.
  while (true) {
    auto ready = epoll.wait(-1); // -1 = block indefinitely until something happens
    if (!ready) {
      LOG_ERROR("epoll_wait failed: %s", ready.error().message.c_str());
      break;
    }

    const int n = ready.value();
    for (int i = 0; i < n; ++i) {
      const epoll_event &ev = epoll.event_at(i);
      const int fd = ev.data.fd;

      if (fd == listener.fd()) {
        accept_new_clients(listener, epoll);
        continue;
      }

      if (fd == timer.fd()) {
        on_tick(epoll, timer.consume_expirations());
        continue;
      }

      // Writable first: draining the outbound queue frees memory and may disarm
      // EPOLLOUT before we read more work in.
      if (ev.events & EPOLLOUT) {
        on_writable(fd, epoll);
      }

      // Then readable. This MUST come before the hangup check: a peer that sends
      // its last message and immediately closes produces EPOLLIN|EPOLLHUP in the
      // same event, and dropping on HUP first would silently discard those final
      // bytes. Reading first also turns a clean close into recv() == 0, which is
      // the precise, informative reason -- "hangup" is the catch-all.
      if (ev.events & EPOLLIN) {
        on_readable(fd, epoll);
      }

      // EPOLLHUP: peer hung up. EPOLLERR: socket error. Only act if the client is
      // still around -- on_readable may already have dropped it.
      if ((ev.events & (EPOLLHUP | EPOLLERR)) && clients_.count(fd) > 0) {
        drop_client(fd, epoll, "hangup or socket error");
      }
    }
  }
}

void Server::accept_new_clients(Socket &listener, Epoll &epoll) {
  // One epoll notification can cover MULTIPLE pending connections, so we must loop.
  // Accepting only one per wakeup would leave clients stuck in the backlog.
  while (true) {
    auto accepted = listener.accept_one();

    if (!accepted) {
      if (accepted.error().code == ErrorCode::WouldBlock) {
        return; // backlog drained -- the normal exit
      }
      LOG_ERROR("accept failed: %s", accepted.error().message.c_str());
      return;
    }

    Socket client = std::move(accepted.value());

    auto nb = client.set_nonblocking();
    if (!nb) {
      LOG_ERROR("set_nonblocking failed: %s", nb.error().message.c_str());
      continue; // client's destructor closes the fd here
    }

    auto sndbuf = client.set_send_buffer(kSocketSendBuffer);
    if (!sndbuf) {
      LOG_ERROR("set_send_buffer failed: %s", sndbuf.error().message.c_str());
      continue;
    }

    const int fd = client.fd();
    auto added = epoll.add(fd, EPOLLIN);
    if (!added) {
      LOG_ERROR("epoll_ctl(add client) failed: %s", added.error().message.c_str());
      continue;
    }

    clients_.emplace(fd, ClientSession{std::move(client), {}}); // map owns the socket
    LOG_INFO("client connected (fd=%d, total=%zu)", fd, clients_.size());
  }
}

void Server::on_readable(int fd, Epoll &epoll) {
  auto it = clients_.find(fd);
  if (it == clients_.end()) {
    return; // already dropped earlier in this same batch of events
  }
  ClientSession &session = it->second;

  uint8_t buffer[kRecvBufferSize];
  std::vector<proto::Frame> frames;

  // Drain what is available. Level-triggered epoll would re-notify us if we stopped
  // early, but draining now saves a trip through epoll_wait.
  while (true) {
    ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);

    if (n > 0) {
      // Hand the raw bytes to this client's parser. It emits only whole frames;
      // anything partial stays buffered inside it until the rest arrives.
      auto parsed = session.parser.feed(buffer, static_cast<size_t>(n), frames);
      if (!parsed) {
        // Unrecoverable framing corruption -- we no longer know where messages
        // begin, so the only safe action is to close the connection.
        drop_client(fd, epoll, parsed.error().message.c_str());
        return;
      }
      for (const auto &frame : frames) {
        on_frame(fd, frame, epoll);
      }
      if (clients_.find(fd) == clients_.end()) {
        return; // on_frame dropped this client (e.g. it fell too far behind)
      }
      frames.clear();
      continue; // there may be more queued
    }

    if (n == 0) {
      drop_client(fd, epoll, "client closed connection");
      return;
    }

    // n == -1
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return; // nothing left right now: hand the thread back to the loop
    }
    if (errno == EINTR) {
      continue; // interrupted by a signal, retry
    }

    drop_client(fd, epoll, std::strerror(errno));
    return;
  }
}

void Server::on_frame(int fd, const proto::Frame &frame, Epoll &epoll) {
  auto it = clients_.find(fd);
  if (it == clients_.end()) {
    return;
  }
  ClientSession &session = it->second;

  LOG_INFO("fd=%d frame type=%u payload=%u bytes", fd,
           static_cast<unsigned>(frame.header.type), frame.header.length);

  switch (frame.header.type) {
  case proto::FrameType::Ping: {
    auto pong = proto::encode_frame(proto::FrameType::Pong, nullptr, 0);
    queue_send(fd, session, pong, epoll);
    break;
  }
  case proto::FrameType::Hello: {
    // Subscribe. We do NOT push audio here -- the timer does, at real-time rate.
    // Blasting the whole file as fast as the socket accepts it would just move the
    // buffering problem to the client.
    session.streaming = true;
    session.audio_pos = 0;
    LOG_INFO("fd=%d subscribed to stream", fd);
    break;
  }
  default:
    break;
  }
}

void Server::queue_send(int fd, ClientSession &session,
                        const std::vector<uint8_t> &bytes, Epoll &epoll) {
  session.send_buffer.insert(session.send_buffer.end(), bytes.begin(), bytes.end());

  if (!flush_send_buffer(fd, session, epoll)) {
    return; // client was dropped; session is gone
  }

  // Enforce the cap AFTER attempting a flush, so a fast client is never penalised.
  if (session.pending_bytes() > max_send_buffer_) {
    drop_client(fd, epoll, "slow consumer: fell more than 2s behind the stream");
  }
}

bool Server::flush_send_buffer(int fd, ClientSession &session, Epoll &epoll) {
  while (session.send_pos < session.send_buffer.size()) {
    const size_t remaining = session.send_buffer.size() - session.send_pos;

    // MSG_NOSIGNAL: without it, writing to a socket the peer already closed raises
    // SIGPIPE, whose default action terminates the process. One rude client would
    // kill the whole server.
    ssize_t n = ::send(fd, session.send_buffer.data() + session.send_pos, remaining,
                       MSG_NOSIGNAL);

    if (n > 0) {
      session.send_pos += static_cast<size_t>(n);
      continue;
    }

    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      // The kernel's socket buffer is full: this client is not reading fast enough.
      // Keep the remainder queued and ask epoll to tell us when it drains.
      if (!session.watching_write) {
        auto st = epoll.mod(fd, EPOLLIN | EPOLLOUT);
        if (!st) {
          drop_client(fd, epoll, st.error().message.c_str());
          return false;
        }
        session.watching_write = true;
      }
      break;
    }

    if (n == -1 && errno == EINTR) {
      continue;
    }

    drop_client(fd, epoll, std::strerror(errno));
    return false;
  }

  // Everything went out. Disarm EPOLLOUT -- a socket with room is almost ALWAYS
  // writable, so leaving it armed makes epoll_wait return immediately forever and
  // spins the CPU at 100%. Arm it only while there is something to write.
  if (session.send_pos == session.send_buffer.size()) {
    session.send_buffer.clear();
    session.send_pos = 0;
    if (session.watching_write) {
      auto st = epoll.mod(fd, EPOLLIN);
      if (!st) {
        drop_client(fd, epoll, st.error().message.c_str());
        return false;
      }
      session.watching_write = false;
    }
  } else if (session.send_pos > session.send_buffer.size() / 2) {
    // Drop the already-sent prefix so the buffer does not grow without bound.
    session.send_buffer.erase(session.send_buffer.begin(),
                              session.send_buffer.begin() +
                                  static_cast<long>(session.send_pos));
    session.send_pos = 0;
  }
  return true;
}

void Server::on_tick(Epoll &epoll, uint64_t missed_ticks) {
  if (missed_ticks == 0) {
    return;
  }
  if (missed_ticks > 1) {
    // The loop did not keep up with the clock. Worth surfacing: it means some
    // handler is taking longer than a tick.
    LOG_ERROR("event loop fell behind: %llu ticks in one wakeup",
              static_cast<unsigned long long>(missed_ticks));
  }

  // Send one tick's worth of audio, never more -- catching up on missed ticks would
  // burst data at a client that is already struggling.
  const size_t chunk_bytes = audio_.bytes_for_ms(kTickMs);
  if (chunk_bytes == 0) {
    return;
  }

  // Collect fds first: queue_send can drop a client, and erasing from clients_
  // while iterating it would invalidate the iterator.
  std::vector<int> streaming_fds;
  streaming_fds.reserve(clients_.size());
  for (const auto &entry : clients_) {
    if (entry.second.streaming) {
      streaming_fds.push_back(entry.first);
    }
  }

  for (const int fd : streaming_fds) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
      continue;
    }
    ClientSession &session = it->second;

    size_t new_pos = 0;
    auto pcm = audio_.read_at(session.audio_pos, chunk_bytes, new_pos);
    session.audio_pos = new_pos;

    auto encoded = proto::encode_frame(proto::FrameType::AudioChunk, pcm.data(),
                                       static_cast<uint32_t>(pcm.size()));
    queue_send(fd, session, encoded, epoll);
  }
}

void Server::on_writable(int fd, Epoll &epoll) {
  auto it = clients_.find(fd);
  if (it == clients_.end()) {
    return;
  }
  flush_send_buffer(fd, it->second, epoll);
}

void Server::drop_client(int fd, Epoll &epoll, const char *why) {
  epoll.del(fd);      // stop watching before the fd goes away
  clients_.erase(fd); // erasing destroys the Socket, which closes the fd
  LOG_INFO("client dropped (fd=%d): %s", fd, why);
}
