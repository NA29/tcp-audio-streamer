// Standalone test for the framing layer. No test framework: this is the whole harness.
#include "proto/parser.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

using namespace proto;

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static std::vector<uint8_t> make_frame(FrameType type, const std::string &body) {
  return encode_frame(type, reinterpret_cast<const uint8_t *>(body.data()),
                      static_cast<uint32_t>(body.size()));
}

static std::string payload_of(const Frame &f) {
  return std::string(f.payload.begin(), f.payload.end());
}

// ---------------------------------------------------------------------------

static void test_single_frame_single_read() {
  std::printf("single frame, one read\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto bytes = make_frame(FrameType::Hello, "hello");

  auto st = parser.feed(bytes.data(), bytes.size(), out);
  CHECK(st.is_ok(), "should parse");
  CHECK(out.size() == 1, "one frame expected");
  CHECK(payload_of(out[0]) == "hello", "payload preserved");
}

// The worst case TCP can hand you: every byte in its own recv().
static void test_byte_at_a_time() {
  std::printf("single frame, ONE BYTE PER READ\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto bytes = make_frame(FrameType::AudioChunk, "fragmented payload data");

  for (size_t i = 0; i < bytes.size(); ++i) {
    auto st = parser.feed(&bytes[i], 1, out);
    CHECK(st.is_ok(), "no error mid-frame");
    // Nothing may be emitted until the very last byte arrives.
    if (i + 1 < bytes.size()) {
      CHECK(out.empty(), "must not emit a partial frame");
    }
  }
  CHECK(out.size() == 1, "exactly one frame after the last byte");
  CHECK(payload_of(out[0]) == "fragmented payload data", "payload intact");
}

static void test_many_frames_one_read() {
  std::printf("three frames, one read\n");
  FrameParser parser;
  std::vector<Frame> out;
  std::vector<uint8_t> stream;
  for (const char *s : {"one", "two", "three"}) {
    auto f = make_frame(FrameType::Hello, s);
    stream.insert(stream.end(), f.begin(), f.end());
  }

  auto st = parser.feed(stream.data(), stream.size(), out);
  CHECK(st.is_ok(), "should parse");
  CHECK(out.size() == 3, "all three extracted from one read");
  CHECK(payload_of(out[0]) == "one" && payload_of(out[2]) == "three", "order preserved");
}

// 2.5 frames: the trailing half must be buffered, not lost and not emitted.
static void test_partial_tail() {
  std::printf("two and a half frames\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto a = make_frame(FrameType::Hello, "alpha");
  auto b = make_frame(FrameType::Hello, "bravo");
  auto c = make_frame(FrameType::Hello, "charlie");

  std::vector<uint8_t> stream;
  stream.insert(stream.end(), a.begin(), a.end());
  stream.insert(stream.end(), b.begin(), b.end());
  stream.insert(stream.end(), c.begin(), c.begin() + 7); // truncated mid-frame

  auto st = parser.feed(stream.data(), stream.size(), out);
  CHECK(st.is_ok(), "partial tail is not an error");
  CHECK(out.size() == 2, "only the complete frames come out");
  CHECK(parser.buffered_bytes() == 7, "the 7 dangling bytes are retained");

  // now deliver the rest
  st = parser.feed(c.data() + 7, c.size() - 7, out);
  CHECK(st.is_ok(), "completion parses");
  CHECK(out.size() == 3, "third frame emerges");
  CHECK(payload_of(out[2]) == "charlie", "reassembled correctly");
}

// Unknown type is RECOVERABLE: skip exactly length bytes, stay in sync.
static void test_unknown_type_stays_in_sync() {
  std::printf("unknown frame type -> skipped, stream stays aligned\n");
  FrameParser parser;
  std::vector<Frame> out;

  auto unknown = encode_frame(static_cast<FrameType>(9999),
                              reinterpret_cast<const uint8_t *>("ignore me"), 9);
  auto good = make_frame(FrameType::Hello, "after");

  std::vector<uint8_t> stream(unknown.begin(), unknown.end());
  stream.insert(stream.end(), good.begin(), good.end());

  auto st = parser.feed(stream.data(), stream.size(), out);
  CHECK(st.is_ok(), "unknown type must not be fatal");
  CHECK(out.size() == 1, "only the known frame is emitted");
  CHECK(payload_of(out[0]) == "after", "the NEXT frame still parses -> no desync");
}

// Bad magic is UNRECOVERABLE: we no longer know where frames begin.
static void test_bad_magic_is_fatal() {
  std::printf("corrupt magic -> fatal\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto bytes = make_frame(FrameType::Hello, "x");
  bytes[0] ^= 0xFF; // corrupt the magic

  auto st = parser.feed(bytes.data(), bytes.size(), out);
  CHECK(!st.is_ok(), "must report an error");
  CHECK(st.error().code == ErrorCode::ProtocolBadMagic, "correct error code");
}

// A huge declared length must be rejected BEFORE any allocation.
static void test_oversized_length_rejected() {
  std::printf("declared length 0xFFFFFFFF -> rejected, no allocation\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto bytes = make_frame(FrameType::Hello, "");
  bytes[8] = 0xFF; bytes[9] = 0xFF; bytes[10] = 0xFF; bytes[11] = 0xFF;

  auto st = parser.feed(bytes.data(), bytes.size(), out);
  CHECK(!st.is_ok(), "must report an error");
  CHECK(st.error().code == ErrorCode::ProtocolFrameTooLarge, "correct error code");
}

static void test_zero_length_payload() {
  std::printf("zero-length payload\n");
  FrameParser parser;
  std::vector<Frame> out;
  auto bytes = make_frame(FrameType::Ping, "");
  auto st = parser.feed(bytes.data(), bytes.size(), out);
  CHECK(st.is_ok(), "should parse");
  CHECK(out.size() == 1 && out[0].payload.empty(), "empty payload is legal");
}

// Randomized: 200 frames sliced at random boundaries. This is the one that catches
// off-by-one bugs a hand-written case never would.
static void test_randomized_chunking() {
  std::printf("200 frames, randomized chunk boundaries\n");
  std::mt19937 rng(12345); // fixed seed -> deterministic, reproducible failures

  std::vector<uint8_t> stream;
  std::vector<std::string> expected;
  for (int i = 0; i < 200; ++i) {
    std::string body(rng() % 300, static_cast<char>('a' + (i % 26)));
    expected.push_back(body);
    auto f = make_frame(FrameType::AudioChunk, body);
    stream.insert(stream.end(), f.begin(), f.end());
  }

  FrameParser parser;
  std::vector<Frame> out;
  size_t pos = 0;
  while (pos < stream.size()) {
    size_t chunk = 1 + rng() % 512;
    chunk = std::min(chunk, stream.size() - pos);
    auto st = parser.feed(stream.data() + pos, chunk, out);
    CHECK(st.is_ok(), "no spurious errors");
    pos += chunk;
  }

  CHECK(out.size() == expected.size(), "every frame recovered");
  bool all_match = out.size() == expected.size();
  for (size_t i = 0; all_match && i < out.size(); ++i) {
    if (payload_of(out[i]) != expected[i]) { all_match = false; }
  }
  CHECK(all_match, "payloads correct and in order");
  CHECK(parser.buffered_bytes() == 0, "nothing left buffered");
}

int main() {
  test_single_frame_single_read();
  test_byte_at_a_time();
  test_many_frames_one_read();
  test_partial_tail();
  test_unknown_type_stays_in_sync();
  test_bad_magic_is_fatal();
  test_oversized_length_rejected();
  test_zero_length_payload();
  test_randomized_chunking();

  if (g_failures == 0) {
    std::printf("\nall parser tests passed\n");
    return 0;
  }
  std::printf("\n%d check(s) failed\n", g_failures);
  return 1;
}
