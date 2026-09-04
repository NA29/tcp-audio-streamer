#pragma once
#include "core/result.h"
#include "proto/frame.h"

#include <cstdint>
#include <vector>

namespace proto {

// ---------------------------------------------------------------------------
// One FrameParser per connected client.
//
// Why it must exist at all: with a blocking server, "how far through this message
// am I" lived implicitly in where the thread was parked inside recv(). An event
// loop has no such stack frame -- on_readable() reads whatever bytes happen to be
// there and returns. So the progress has to be stored explicitly. This class is
// that storage.
//
// It is fed arbitrary byte runs and emits whole frames. It must handle:
//   - a header split across several recv() calls
//   - a payload split across several recv() calls
//   - several complete frames arriving in one recv() call
//   - any combination of the above (e.g. 2.5 frames in one read)
//
// Error policy -- the important design decision:
//   RECOVERABLE   unknown frame type. The length field is still trustworthy, so we
//                 can skip exactly that many bytes and stay aligned with the sender.
//                 This is what lets old servers ignore new message types.
//   UNRECOVERABLE bad magic, or a length beyond kMaxPayloadSize. Both mean we no
//                 longer know where the next frame begins. There is no safe way to
//                 resynchronize a binary stream by guessing, so the caller must
//                 close the connection. Scanning forward for the next magic value
//                 would be a guess, and a guess is how you silently desync.
// ---------------------------------------------------------------------------
class FrameParser {
public:
  // Appends `len` bytes of raw socket data and extracts every complete frame it can.
  // Completed frames are appended to `out`.
  // Returns an Error only for unrecoverable framing corruption; the caller then
  // drops the client.
  Status feed(const uint8_t *data, size_t len, std::vector<Frame> &out);

  size_t buffered_bytes() const { return buffer_.size() - read_pos_; }

private:
  // Tries to pull one frame from the buffer.
  // out_needs_more is set when the buffer simply does not hold a whole frame yet.
  Status try_extract_one(std::vector<Frame> &out, bool &out_needs_more);

  void compact();

  std::vector<uint8_t> buffer_; // bytes received but not yet consumed
  size_t read_pos_ = 0;         // how far into buffer_ we have consumed
};

} // namespace proto
