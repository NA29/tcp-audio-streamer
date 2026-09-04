#pragma once
#include "core/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace audio {

// A .wav file is RIFF-chunked:
//   "RIFF" <size> "WAVE"  then a sequence of  <4-byte id> <4-byte size> <payload>
// We need two of those chunks: "fmt " (the format) and "data" (the samples).
// Chunk ids we do not recognise (LIST, INFO, ...) are skipped by their length --
// same idea as the unknown-frame-type rule in the wire protocol.
struct WavFormat {
  uint16_t channels = 2;
  uint32_t sample_rate = 44100;
  uint16_t bits_per_sample = 16;

  uint32_t bytes_per_second() const {
    return sample_rate * channels * (bits_per_sample / 8u);
  }
};

class AudioSource {
public:
  static Result<AudioSource> from_wav(const std::string &path);

  // Fallback so the server runs with no asset on disk: a 440 Hz sine, 2s, looped.
  static AudioSource synthetic_tone();

  AudioSource(WavFormat fmt, std::vector<uint8_t> pcm);

  const WavFormat &format() const { return format_; }
  size_t total_bytes() const { return pcm_.size(); }

  // How many bytes represent `ms` of audio at this format -- this is what makes
  // playback real-time rather than "as fast as the socket will take it".
  size_t bytes_for_ms(uint32_t ms) const;

  // Next `n` bytes starting at `pos`, wrapping at the end. Returns the new position.
  // Per-client position, so clients can join at any time without sharing a cursor.
  std::vector<uint8_t> read_at(size_t pos, size_t n, size_t &new_pos) const;

private:
  WavFormat format_;
  std::vector<uint8_t> pcm_;
};

} // namespace audio
