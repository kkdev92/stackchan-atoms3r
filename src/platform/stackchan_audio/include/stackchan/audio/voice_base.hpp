#pragma once

#include "stackchan/domain/audio_direction.hpp"
#include "stackchan/ports/audio.hpp"

// Audio on the Atomic Voice Base: an ES8311 codec driven over I2S, with
// I2C for its control registers.
//
// Wiring
// ------
//   G39 SCL / G38 SDA   codec control (I2C)
//   G5  DIN             device to codec (playback)
//   G6  LRCK / WS
//   G7  DOUT            codec to device (recording)
//   G8  SCK / BCLK
//
// Adapter operation is half duplex
// --------------------------------
// The microphone and speaker share one codec and are not opened together.
//
// The rule itself lives in domain::AudioDirection, as a state table with
// host tests. This type just does what the table says: write the codec
// registers and set up I2S.
//
// When one direction is running, a request for the other is refused until the
// active direction is stopped.
//
// Self-check
// ----------
// Recording is measured directly. Playback verification requires a listener
// because capture and playback cannot run simultaneously.

namespace stackchan::audio {

// The sample rate used for conversation.
//
// The current device/gateway protocol and buffers use 16 kHz; other rates are
// not accepted by this adapter.
inline constexpr std::uint32_t kVoiceSampleRate = 16000;

class VoiceBase final : public ports::AudioSource, public ports::AudioSink {
 public:
  VoiceBase() = default;
  ~VoiceBase() override = default;

  // Bring up the codec and I2S. False when no Voice Base is attached.
  [[nodiscard]] bool begin();
  [[nodiscard]] bool available() const noexcept { return available_; }

  // --- ports::AudioSource
  [[nodiscard]] bool start_capture(std::uint32_t sample_rate) override;
  void stop_capture() override;
  [[nodiscard]] bool capturing() const override { return direction_.capturing(); }
  [[nodiscard]] std::size_t read(ports::Sample* out, std::size_t count,
                                 runtime::Deadline deadline,
                                 std::uint32_t now_ms) override;

  // --- ports::AudioSink
  [[nodiscard]] bool start_playback(std::uint32_t sample_rate) override;
  void stop_playback() override;
  [[nodiscard]] bool playing() const override { return direction_.playing(); }
  [[nodiscard]] std::size_t write(const ports::Sample* samples, std::size_t count,
                                  runtime::Deadline deadline,
                                  std::uint32_t now_ms) override;
  void set_volume(std::uint8_t percent) override;

  // Check on real hardware that the microphone is alive.
  //
  // Playback is not part of this, because the two cannot run together.
  struct MicrophoneCheck {
    bool ran = false;
    // How many samples arrived. Zero means "nothing is getting through",
    // which is a different fault from "the room is quiet", so the two are
    // reported separately.
    std::size_t samples = 0;
    // Root mean square, 0 to 32767.
    std::uint16_t rms = 0;

    // Whether samples are arriving at all. Whether there was any sound
    // depends on the room, so it is not part of this judgement.
    [[nodiscard]] bool receiving() const noexcept { return ran && samples > 0; }
  };
  [[nodiscard]] MicrophoneCheck check_microphone();

  // Play a short tone, returning how many samples were written.
  //
  // Whether the speaker works can only be confirmed by someone listening.
  // It cannot be checked by recording it, since both cannot run at once.
  [[nodiscard]] std::size_t play_tone(std::uint32_t hz, std::uint32_t duration_ms);

 private:
  bool available_ = false;
  // The half-duplex state table. Serialising the calls happens in the .cpp;
  // what is permitted once they are serialised is decided here.
  domain::AudioDirection direction_;
  std::uint32_t sample_rate_ = kVoiceSampleRate;
  // 100% means unity gain: samples come out as they arrived.
  std::uint8_t volume_ = 100;
};

}  // namespace stackchan::audio
