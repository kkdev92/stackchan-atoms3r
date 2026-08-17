#pragma once

#include <cstddef>
#include <cstdint>

#include "stackchan/runtime/deadline.hpp"

// Where audio enters and leaves.
//
// What is abstracted
// ------------------
// Only "how many PCM samples were read, or written". No codec part number
// and no I2S configuration appear here. Swapping the ES8311 for something
// else does not change how a conversation proceeds.
//
// Why the format is fixed
// -----------------------
// 16-bit signed, mono, decided once. The Voice Base has a single
// microphone and one playback path. Making it configurable would blur the
// question of where conversion happens, and let each caller arrive with a
// different assumption. Only the sample rate stays open, because it has to
// match whatever the gateway's STT and TTS expect.
//
// Deadlines are mandatory
// -----------------------
// Reading and writing are waits, so no unbounded variant is offered. Offer
// one and eventually someone calls it in a way that hangs (invariant 4).

namespace stackchan::ports {

// One 16-bit signed mono sample.
using Sample = std::int16_t;

class AudioSource {
 public:
  virtual ~AudioSource() = default;

  AudioSource(const AudioSource&) = delete;
  AudioSource& operator=(const AudioSource&) = delete;
  AudioSource(AudioSource&&) = delete;
  AudioSource& operator=(AudioSource&&) = delete;

  // Start capturing. Does nothing if capture is already running.
  [[nodiscard]] virtual bool start_capture(std::uint32_t sample_rate) = 0;
  virtual void stop_capture() = 0;
  [[nodiscard]] virtual bool capturing() const = 0;

  // Returns how many samples were read. If the deadline passes first,
  // returns whatever had arrived by then.
  //
  // Zero means "nothing yet", not an error. Callers are expected to keep
  // asking.
  [[nodiscard]] virtual std::size_t read(Sample* out, std::size_t count,
                                         runtime::Deadline deadline,
                                         std::uint32_t now_ms) = 0;

 protected:
  AudioSource() = default;
};

class AudioSink {
 public:
  virtual ~AudioSink() = default;

  AudioSink(const AudioSink&) = delete;
  AudioSink& operator=(const AudioSink&) = delete;
  AudioSink(AudioSink&&) = delete;
  AudioSink& operator=(AudioSink&&) = delete;

  [[nodiscard]] virtual bool start_playback(std::uint32_t sample_rate) = 0;

  // Waits for buffered audio to finish before stopping. Cutting it short
  // clips the end of whatever was playing.
  virtual void stop_playback() = 0;
  [[nodiscard]] virtual bool playing() const = 0;

  // Returns how many samples were written. If the deadline passes first,
  // returns whatever fit by then.
  [[nodiscard]] virtual std::size_t write(const Sample* samples, std::size_t count,
                                          runtime::Deadline deadline,
                                          std::uint32_t now_ms) = 0;

  // 0 to 100. Values outside the range are clamped.
  virtual void set_volume(std::uint8_t percent) = 0;

 protected:
  AudioSink() = default;
};

}  // namespace stackchan::ports
