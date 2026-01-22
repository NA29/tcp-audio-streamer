PROJECT: TCP Audio Streaming Server (C++ / TCP / POSIX / Systems)

OVERVIEW:
Build a robust, multi-client TCP audio streaming server in C++ with explicit RAII ownership, binary protocol framing, backpressure, and abuse protection. The project prioritizes correctness, determinism, and systems-level behavior over features.

==================================================
EPIC 1: Core Utilities (RAII Foundation)
GOAL: Ownership and cleanup correctness

TASKS:
- Implement Socket RAII class
  - Wraps POSIX file descriptor (int fd)
  - Closes fd in destructor
  - Non-copyable
  - Movable
- Implement simple error handling mechanism
  - Either Result<T> or explicit error codes
- Implement basic logging macros
  - LOG_INFO
  - LOG_ERROR

DONE WHEN:
- Socket resources are released automatically on scope exit
- No raw close() calls exist outside RAII wrappers
- Move semantics are implemented and tested

==================================================
EPIC 2: TCP Networking Core
GOAL: Accept connections and send raw bytes

TASKS:
- Implement TCP server
  - socket()
  - bind()
  - listen()
  - accept()
- Handle one client at a time
- Implement blocking send() and recv()
- Correctly handle partial reads and partial writes

DONE WHEN:
- Client can successfully connect to server
- Server logs "client connected"
- Server can send arbitrary bytes to client
- Server does not crash on client disconnect

==================================================
EPIC 3: TCP Client
GOAL: Validate server correctness

TASKS:
- Implement TCP client
  - Connect to server
  - Receive raw bytes
- Print received byte counts
- Handle server disconnect cleanly

DONE WHEN:
- Client connects reliably
- Client survives server restarts
- Client makes no assumptions about buffer sizes

==================================================
EPIC 4: Binary Protocol and Framing
GOAL: Deterministic message parsing

TASKS:
- Define binary protocol
  - Message header
  - Payload size
  - Message type
- Implement receive buffer
- Handle partial messages
- Reject malformed messages

DONE WHEN:
- Protocol survives partial recv() calls
- Invalid input does not crash server
- Client and server remain in sync

==================================================
EPIC 5: Audio Source and Streaming
GOAL: Stream real data, not dummy bytes

TASKS:
- Load WAV or PCM audio file
- Read audio in fixed-size chunks
- Stream chunks to clients
- Ensure streaming logic does not block server

DONE WHEN:
- Client receives continuous audio data
- Disk I/O does not block networking
- Stream timing is stable

==================================================
EPIC 6: Multi-Client Support
GOAL: Robust concurrency model (single-threaded or select/poll)

TASKS:
- Track connected clients
- Maintain per-client state
- Clean up client state on disconnect
- Enforce maximum client count

DONE WHEN:
- Multiple clients can connect simultaneously
- One client disconnect does not affect others
- Server remains stable

==================================================
EPIC 7: Backpressure and Bandwidth Control
GOAL: Prevent slow clients from degrading server

TASKS:
- Add per-client send buffers
- Detect slow or stalled clients
- Throttle or drop lagging clients
- Cap per-client buffer sizes

DONE WHEN:
- Slow clients do not block others
- Memory usage remains bounded
- Server remains responsive under load

==================================================
EPIC 8: Limits and Abuse Protection
GOAL: Production mindset and safety

TASKS:
- Enforce maximum connection duration
- Enforce maximum bytes per second per client
- Enforce maximum pending buffer size
- Implement graceful shutdown handling

DONE WHEN:
- Abuse scenarios do not crash server
- Limits are enforced deterministically

==================================================
EPIC 9: Observability and Diagnostics
GOAL: Explain system behavior clearly

TASKS:
- Log connection lifecycle events
- Track throughput metrics
- Log drop reasons
- Maintain error counters

DONE WHEN:
- Exact reasons for client drops are observable
- Logs are readable and actionable

==================================================
EPIC 10: Polish and Documentation
GOAL: Resume-ready quality

TASKS:
- Write README
  - Architecture
  - Protocol specification
  - Failure modes
- Perform code cleanup
- Stress testing
- Comment non-obvious design decisions

DONE WHEN:
- Project can be handed to another engineer
- System can be demoed live

