#include "proto/parser.h"

#include <cstring>

namespace proto {

Status FrameParser::feed(const uint8_t *data, size_t len, std::vector<Frame> &out) {
  buffer_.insert(buffer_.end(), data, data + len);

  // Keep pulling frames until the buffer no longer holds a complete one. One recv()
  // can easily contain several frames, and stopping after the first would leave them
  // sitting in the buffer until more data happened to arrive.
  while (true) {
    bool needs_more = false;
    auto status = try_extract_one(out, needs_more);
    if (!status) {
      return status.error(); // unrecoverable: caller drops the connection
    }
    if (needs_more) {
      break;
    }
  }

  compact();
  return Unit{};
}

Status FrameParser::try_extract_one(std::vector<Frame> &out, bool &out_needs_more) {
  const size_t available = buffer_.size() - read_pos_;

  // Not even a full header yet -- park the bytes and wait for the next read.
  if (available < kHeaderSize) {
    out_needs_more = true;
    return Unit{};
  }

  const uint8_t *base = buffer_.data() + read_pos_;
  const FrameHeader header = decode_header(base);

  // ---- validate BEFORE trusting the length field ----

  if (header.magic != kMagic) {
    // We are not where we thought we were in the stream. Unrecoverable.
    return Error{ErrorCode::ProtocolBadMagic, "bad magic: stream desynchronized"};
  }

  if (header.length > kMaxPayloadSize) {
    // Never allocate based on an unvalidated length. This check is what stops a
    // 12-byte packet claiming length=0xFFFFFFFF from triggering a 4 GB allocation.
    return Error{ErrorCode::ProtocolFrameTooLarge, "declared payload exceeds cap"};
  }

  const size_t total = kHeaderSize + header.length;

  // Header is complete but the payload has not fully arrived. Wait -- and note we
  // have NOT consumed anything, so the next feed() re-reads this same header.
  if (available < total) {
    out_needs_more = true;
    return Unit{};
  }

  // ---- a whole frame is present ----

  if (!is_known_type(static_cast<uint16_t>(header.type))) {
    // Recoverable: the length is valid, so we can skip exactly this frame and stay
    // byte-aligned with the sender. Forward compatibility lives here.
    read_pos_ += total;
    out_needs_more = false;
    return Unit{};
  }

  Frame frame;
  frame.header = header;
  frame.payload.assign(base + kHeaderSize, base + total);
  out.push_back(std::move(frame));

  read_pos_ += total;
  out_needs_more = false;
  return Unit{};
}

void FrameParser::compact() {
  if (read_pos_ == 0) {
    return;
  }
  // Erasing consumed bytes on every frame would be an O(n) memmove each time. Instead
  // we advance read_pos_ and only physically shift when the dead prefix is more than
  // half the buffer -- amortized O(1) per byte.
  if (read_pos_ == buffer_.size()) {
    buffer_.clear();
    read_pos_ = 0;
    return;
  }
  if (read_pos_ > buffer_.size() / 2) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(read_pos_));
    read_pos_ = 0;
  }
}

} // namespace proto
