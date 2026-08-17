#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Building a WAV header.
//
// Recorded audio is sent to the gateway as a plain 44-byte-header WAV, fixed
// at 16 kHz, 16-bit, mono. Fixed rather than configurable for the same
// reason as the audio ports: a format that can vary leaves it unclear where
// conversion is supposed to happen.
//
// The 44 bytes are the RIFF header, a PCM fmt chunk and a data chunk, all
// little-endian.
//
// It lives in core so that the byte layout can be checked on the host;
// sending it is the conversation code's job.

namespace stackchan::domain {

inline constexpr std::size_t kWavHeaderBytes = 44;

// Write the header for 16 kHz 16-bit mono PCM into out. pcm_bytes is the
// length of the PCM that will follow, which is the sample count times two.
void write_wav_header(std::array<std::uint8_t, kWavHeaderBytes>& out,
                      std::uint32_t pcm_bytes) noexcept;

}  // namespace stackchan::domain
