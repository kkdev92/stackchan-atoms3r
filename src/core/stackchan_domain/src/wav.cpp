#include "stackchan/domain/wav.hpp"

namespace stackchan::domain {
namespace {

// Fixed format, matching what the audio ports declare.
constexpr std::uint32_t kSampleRate = 16000;
constexpr std::uint16_t kChannels = 1;
constexpr std::uint16_t kBitsPerSample = 16;

void put_u16(std::array<std::uint8_t, kWavHeaderBytes>& out, std::size_t at,
             std::uint16_t value) noexcept {
  out[at] = static_cast<std::uint8_t>(value & 0xFF);
  out[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put_u32(std::array<std::uint8_t, kWavHeaderBytes>& out, std::size_t at,
             std::uint32_t value) noexcept {
  out[at] = static_cast<std::uint8_t>(value & 0xFF);
  out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  out[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  out[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

// Write a four-character tag such as "RIFF".
//
// The parameter is a reference to a C array so that the length is checked
// at compile time. With a string_view, a three-character tag would compile
// and quietly produce a WAV whose every subsequent offset is off by one.
void put_tag(std::array<std::uint8_t, kWavHeaderBytes>& out, std::size_t at,
             // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
             const char (&tag)[5]) noexcept {
  for (std::size_t i = 0; i < 4; ++i) {
    out[at + i] = static_cast<std::uint8_t>(tag[i]);
  }
}

}  // namespace

void write_wav_header(std::array<std::uint8_t, kWavHeaderBytes>& out,
                      std::uint32_t pcm_bytes) noexcept {
  const std::uint32_t byte_rate = kSampleRate * kChannels * (kBitsPerSample / 8);
  const std::uint16_t block_align = kChannels * (kBitsPerSample / 8);

  put_tag(out, 0, "RIFF");
  // The RIFF size covers everything after it, so it is the total minus 8.
  put_u32(out, 4, 36 + pcm_bytes);
  put_tag(out, 8, "WAVE");

  put_tag(out, 12, "fmt ");
  put_u32(out, 16, 16);  // a PCM fmt chunk is 16 bytes
  put_u16(out, 20, 1);   // wFormatTag = WAVE_FORMAT_PCM
  put_u16(out, 22, kChannels);
  put_u32(out, 24, kSampleRate);
  put_u32(out, 28, byte_rate);
  put_u16(out, 32, block_align);
  put_u16(out, 34, kBitsPerSample);

  put_tag(out, 36, "data");
  put_u32(out, 40, pcm_bytes);
}

}  // namespace stackchan::domain
