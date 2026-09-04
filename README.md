# tcp-audio-streamer

A single-threaded, non-blocking TCP audio streaming server in C++20. One thread serves
every connected client via `epoll`, streams real-time PCM at the source's native rate, and
isolates slow consumers so they degrade alone instead of stalling the event loop.

```
./scripts/dev.sh image     # build the Linux container (epoll is Linux-only)
./scripts/dev.sh build     # compile
./scripts/dev.sh test      # full suite
./scripts/dev.sh run assets/test_tone.wav
```

## Architecture

```
                    ┌──────────────────────────────────────────┐
                    │              epoll_wait()                │  ← the only place
                    │   the one place this thread ever sleeps  │    the thread blocks
                    └────┬──────────────┬──────────────┬───────┘
                         │              │              │
                  listener fd      timer fd        client fds
                         │              │              │
                    accept() to     20 ms tick:   EPOLLIN → recv → FrameParser
                    EAGAIN          push one          │
                         │          chunk to      EPOLLOUT → flush send queue
                         │          each sub          │
                         └──────────────┴──────────────┘
                                        │
                              ClientSession per fd
                        { socket, parser, send_buffer, audio_pos }
```

Every handler does a slice of work and returns. Nothing blocks, ever.

**Why single-threaded.** Thread-per-client costs ~8 MB of stack each, pays scheduler
context switches, and forces locks on shared state. An event loop trades that for
hand-written state machines: harder code, but no synchronization at all, since no two
clients are ever handled at the same instant. Scaling past one core means running N loops
with disjoint client sets (`SO_REUSEPORT`), not adding threads to this one.

**Why per-client state is explicit.** With blocking I/O, "where am I in this conversation"
lives implicitly in where the thread is parked inside `recv()`. An event loop has no such
stack frame, so that progress moves into `ClientSession` — which is exactly why the
incremental parser and the send queue exist. They are consequences of the architecture,
not features bolted on.

## Wire protocol

TCP is a byte stream: a 100-byte message can arrive as 100 one-byte reads, and three
messages can arrive in one read. Framing is therefore explicit, and length-prefixed rather
than delimiter-based because any byte value is legal inside PCM audio.

```
 offset  size  field
 0       4     magic    0x41554449 "AUDI"
 4       2     type     1=Hello 2=AudioChunk 3=Ping 4=Pong
 6       2     flags    reserved, 0
 8       4     length   payload bytes (excludes this 12-byte header)
 12      N     payload
```

All integers big-endian, so the format is identical on x86 and ARM. Payloads are capped at
1 MiB. Headers are read with `memcpy`, not a struct cast — struct padding and alignment
would make that undefined behaviour, and `memcpy` compiles away at `-O2`.

### Malformed input: recoverable vs not

| Input | Response | Reasoning |
|---|---|---|
| Unknown frame type | Skip exactly `length` bytes, continue | The length is still trustworthy, so we stay byte-aligned. This is how a server ignores message types added later. |
| Bad magic | Close the connection | We no longer know where the next frame begins. Scanning forward for the next magic is a *guess*, and a guess is how a binary stream silently desyncs. |
| `length` > 1 MiB | Reject before allocating | Otherwise a 12-byte packet claiming `length=0xFFFFFFFF` triggers a 4 GB allocation — a one-packet denial of service. |

## Backpressure

`send()` on a non-blocking socket writes only what the kernel buffer will take, then
returns `EAGAIN`. Blocking to finish would stall every other client, so the remainder is
queued per client and flushed when `epoll` reports the socket writable again.

- **`EPOLLOUT` is armed only while bytes are pending.** A socket with room is almost always
  writable, so leaving it armed makes `epoll_wait` return instantly forever and spins the
  CPU at 100%.
- **`MSG_NOSIGNAL` on every `send()`.** Writing to a peer-closed socket otherwise raises
  `SIGPIPE`, whose default action terminates the process — one rude client would kill the
  server.
- **The kernel's own `SO_SNDBUF` is capped at 64 KB.** Linux autotunes it into the megabytes,
  which for a live stream silently parks seconds of stale audio somewhere we cannot see,
  age out, or apply policy to. Capping it keeps the queue in our buffer where `EAGAIN`
  arrives promptly.
- **A client may fall at most 2 seconds behind, then it is dropped.** The limit is expressed
  in *time*, not bytes, and derived from the audio format — 2 s is ~350 KB at 44.1 kHz
  stereo 16-bit but ~32 KB at 8 kHz mono. A fixed byte cap would mean wildly different
  amounts of audio. Unbounded queueing only moves the failure from "slow client stalls
  everyone" to "slow client OOMs the server".

## Streaming

The clock is a `timerfd`, so it is just another descriptor in the same `epoll` set — no
timer thread, no `epoll_wait` timeout arithmetic. Every 20 ms each subscriber is handed
exactly one tick of audio (`bytes_per_second × 20 / 1000`, rounded down to a whole sample
frame so no sample is split across chunks). Missed ticks are logged but never replayed as a
burst: a client that is already behind is the last one that should be sent a catch-up flood.

`CLOCK_MONOTONIC`, not `CLOCK_REALTIME`, so NTP or DST adjustments cannot make the cadence
hiccup. Each client holds its own cursor into the source, so clients may join at any time
and the stream loops indefinitely.

The WAV reader walks RIFF chunks, validating every declared length against the remaining
file size before indexing, and skips unrecognized chunks by their length — the same
forward-compatibility rule as the wire protocol. With no file argument it synthesizes a
440 Hz tone, so the server always runs.

## Tests

`./scripts/dev.sh test`

| Suite | Covers |
|---|---|
| `parserTest` | Frame delivered **one byte per read**; 3 frames in one read; 2.5 frames; unknown-type resync; corrupt magic; `length=0xFFFFFFFF`; zero-length payload; **200 frames sliced at random boundaries** (seeded, so failures reproduce) |
| `smoke_test` | 3 clients served concurrently, out of connect order; one disconnects, others unaffected |
| `e2e_test` | One frame split across three `send()` calls; 3 frames in one write; malformed client dropped while peers keep being served |
| `backpressure_test` | A client that never reads is dropped at the 2 s limit while a concurrent client keeps **sub-millisecond** ping latency |
| `stream_test` | Real WAV parsed and streamed to 2 clients at **176,297 B/s vs 176,400 expected — 0.1 % drift**, 100/100 frames each, zero tick overruns |

## Layout

```
src/core/socket.h|.cpp   RAII fd wrapper: move-only, closes on scope exit
src/core/epoll.h|.cpp    epoll_create1 / epoll_ctl / epoll_wait
src/core/timer.h|.cpp    periodic timerfd
src/core/result.h        Result<T> sum type over a union
src/proto/frame.h|.cpp   wire format, encode/decode
src/proto/parser.h|.cpp  incremental frame parser, one per client
src/audio/source.h|.cpp  RIFF/WAV reader, real-time chunking
src/server/server.h|.cpp the event loop
```

## Known limits

- One event loop, one core. Scaling out means `SO_REUSEPORT` with N processes.
- No TLS, no authentication, no per-IP connection limit.
- The whole PCM file is held in memory; a long file should be streamed from disk.
- Only 16-bit uncompressed PCM WAV is supported.
