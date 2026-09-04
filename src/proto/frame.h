#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// Wire format. TCP is a BYTE STREAM, not a message stream: it guarantees order
// and delivery, but nothing about where one recv() ends. A 100-byte message can
// arrive as 100 one-byte reads, and three messages can arrive in a single read.
// So the protocol itself has to say where each message ends. Two options:
//
//   delimiter-based  (e.g. '\n')  -> must escape the delimiter inside payloads
//   length-prefixed               -> read the length, then read exactly that many bytes
//
// Length-prefixed is the right choice for binary audio, where any byte value is
// legal payload data and there is no safe delimiter.
//
//   offset  size  field
//   0       4     magic   0x41554449 ("AUDI") -- detects desync / wrong protocol
//   4       2     type    FrameType
//   6       2     flags   reserved, must be 0
//   8       4     length  payload byte count (NOT including this 12-byte header)
//   12      N     payload
//
// All multi-byte integers are big-endian (network byte order), so the protocol
// is identical on an x86 laptop and an ARM server.
// ---------------------------------------------------------------------------

namespace proto {

inline constexpr uint32_t kMagic = 0x41554449; // "AUDI"
inline constexpr size_t kHeaderSize = 12;

// Cap on a declared payload length. Without this, a client sending length = 0xFFFFFFFF
// would make us try to allocate 4 GB -- a one-packet denial of service.
inline constexpr uint32_t kMaxPayloadSize = 1u << 20; // 1 MiB

enum class FrameType : uint16_t {
  Hello = 1,      // client -> server: start streaming
  AudioChunk = 2, // server -> client: PCM data
  Ping = 3,
  Pong = 4,
};

struct FrameHeader {
  uint32_t magic = kMagic;
  FrameType type = FrameType::Hello;
  uint16_t flags = 0;
  uint32_t length = 0;
};

struct Frame {
  FrameHeader header;
  std::vector<uint8_t> payload;
};

bool is_known_type(uint16_t raw_type);

// Reads a header out of `data` (which must hold at least kHeaderSize bytes).
// Pure deserialization: it does NOT validate. Validation is the parser's job.
FrameHeader decode_header(const uint8_t *data);

// Serializes header + payload into a flat byte vector ready for send().
std::vector<uint8_t> encode_frame(FrameType type, const uint8_t *payload, uint32_t length);

} // namespace proto
