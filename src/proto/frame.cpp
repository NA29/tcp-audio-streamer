#include "proto/frame.h"

#include <arpa/inet.h> // htonl/ntohl/htons/ntohs
#include <cstring>     // std::memcpy

namespace proto {

bool is_known_type(uint16_t raw_type) {
  switch (static_cast<FrameType>(raw_type)) {
  case FrameType::Hello:
  case FrameType::AudioChunk:
  case FrameType::Ping:
  case FrameType::Pong:
    return true;
  }
  return false;
}

FrameHeader decode_header(const uint8_t *data) {
  FrameHeader header{};
  uint32_t magic_be = 0, length_be = 0;
  uint16_t type_be = 0, flags_be = 0;

  // memcpy rather than a reinterpret_cast to a struct pointer: casting would rely on
  // the compiler's padding/alignment choices and is undefined behaviour on a
  // misaligned buffer. memcpy is free at -O2 and always correct.
  std::memcpy(&magic_be, data + 0, 4);
  std::memcpy(&type_be, data + 4, 2);
  std::memcpy(&flags_be, data + 6, 2);
  std::memcpy(&length_be, data + 8, 4);

  header.magic = ntohl(magic_be); // network (big endian) -> host order
  header.type = static_cast<FrameType>(ntohs(type_be));
  header.flags = ntohs(flags_be);
  header.length = ntohl(length_be);
  return header;
}

std::vector<uint8_t> encode_frame(FrameType type, const uint8_t *payload, uint32_t length) {
  std::vector<uint8_t> out(kHeaderSize + length);

  const uint32_t magic_be = htonl(kMagic);
  const uint16_t type_be = htons(static_cast<uint16_t>(type));
  const uint16_t flags_be = 0;
  const uint32_t length_be = htonl(length);

  std::memcpy(out.data() + 0, &magic_be, 4);
  std::memcpy(out.data() + 4, &type_be, 2);
  std::memcpy(out.data() + 6, &flags_be, 2);
  std::memcpy(out.data() + 8, &length_be, 4);

  if (length > 0 && payload != nullptr) {
    std::memcpy(out.data() + kHeaderSize, payload, length);
  }
  return out;
}

} // namespace proto
