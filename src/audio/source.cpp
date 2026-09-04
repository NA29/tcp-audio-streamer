#include "audio/source.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace audio {
namespace {

uint32_t read_u32_le(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t read_u16_le(const uint8_t *p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               (static_cast<uint16_t>(p[1]) << 8));
}

} // namespace

AudioSource::AudioSource(WavFormat fmt, std::vector<uint8_t> pcm)
    : format_(fmt), pcm_(std::move(pcm)) {}

size_t AudioSource::bytes_for_ms(uint32_t ms) const {
  size_t bytes = (static_cast<size_t>(format_.bytes_per_second()) * ms) / 1000;
  // Round down to a whole frame (all channels of one sample) so we never split a
  // sample across chunks and produce a click.
  const size_t frame = format_.channels * (format_.bits_per_sample / 8u);
  if (frame > 0) {
    bytes -= bytes % frame;
  }
  return bytes;
}

std::vector<uint8_t> AudioSource::read_at(size_t pos, size_t n, size_t &new_pos) const {
  std::vector<uint8_t> out;
  if (pcm_.empty() || n == 0) {
    new_pos = 0;
    return out;
  }
  out.reserve(n);
  size_t cursor = pos % pcm_.size();
  while (out.size() < n) {
    const size_t take = std::min(n - out.size(), pcm_.size() - cursor);
    out.insert(out.end(), pcm_.begin() + static_cast<long>(cursor),
               pcm_.begin() + static_cast<long>(cursor + take));
    cursor = (cursor + take) % pcm_.size(); // wrap: the stream loops forever
  }
  new_pos = cursor;
  return out;
}

AudioSource AudioSource::synthetic_tone() {
  WavFormat fmt{}; // 44100 Hz, stereo, 16-bit
  const uint32_t seconds = 2;
  const size_t samples = fmt.sample_rate * seconds;
  std::vector<uint8_t> pcm(samples * fmt.channels * 2);

  for (size_t i = 0; i < samples; ++i) {
    const double t = static_cast<double>(i) / fmt.sample_rate;
    const auto value = static_cast<int16_t>(12000.0 * std::sin(2.0 * M_PI * 440.0 * t));
    for (uint16_t ch = 0; ch < fmt.channels; ++ch) {
      const size_t offset = (i * fmt.channels + ch) * 2;
      pcm[offset] = static_cast<uint8_t>(value & 0xFF);          // little-endian
      pcm[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    }
  }
  return AudioSource(fmt, std::move(pcm));
}

Result<AudioSource> AudioSource::from_wav(const std::string &path) {
  std::FILE *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return Error{ErrorCode::FileOpenFailed, "cannot open " + path};
  }

  std::vector<uint8_t> bytes;
  uint8_t buf[65536];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0) {
    bytes.insert(bytes.end(), buf, buf + n);
  }
  std::fclose(file);

  // Every length below is validated against the remaining size before we index --
  // a truncated or hostile file must produce an error, never an out-of-bounds read.
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    return Error{ErrorCode::WavMalformed, "not a RIFF/WAVE file"};
  }

  WavFormat fmt{};
  std::vector<uint8_t> pcm;
  bool saw_fmt = false;

  size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const char *id = reinterpret_cast<const char *>(bytes.data() + pos);
    const uint32_t size = read_u32_le(bytes.data() + pos + 4);
    const size_t body = pos + 8;

    if (body + size > bytes.size()) {
      return Error{ErrorCode::WavMalformed, "chunk length runs past end of file"};
    }

    if (std::memcmp(id, "fmt ", 4) == 0) {
      if (size < 16) {
        return Error{ErrorCode::WavMalformed, "fmt chunk too short"};
      }
      const uint16_t audio_format = read_u16_le(bytes.data() + body);
      if (audio_format != 1) { // 1 == uncompressed PCM
        return Error{ErrorCode::WavUnsupported, "only uncompressed PCM is supported"};
      }
      fmt.channels = read_u16_le(bytes.data() + body + 2);
      fmt.sample_rate = read_u32_le(bytes.data() + body + 4);
      fmt.bits_per_sample = read_u16_le(bytes.data() + body + 14);
      saw_fmt = true;
    } else if (std::memcmp(id, "data", 4) == 0) {
      pcm.assign(bytes.begin() + static_cast<long>(body),
                 bytes.begin() + static_cast<long>(body + size));
    }
    // Unknown chunk: skip it by its declared length. Chunks are word-aligned, so an
    // odd size is followed by a pad byte.
    pos = body + size + (size % 2);
  }

  if (!saw_fmt) {
    return Error{ErrorCode::WavMalformed, "no fmt chunk"};
  }
  if (pcm.empty()) {
    return Error{ErrorCode::WavMalformed, "no data chunk"};
  }
  if (fmt.bits_per_sample != 16 || fmt.channels == 0 || fmt.sample_rate == 0) {
    return Error{ErrorCode::WavUnsupported, "expected 16-bit PCM with >0 channels"};
  }

  return AudioSource(fmt, std::move(pcm));
}

} // namespace audio
