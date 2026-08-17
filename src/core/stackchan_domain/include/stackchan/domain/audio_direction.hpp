#pragma once

#include <cstdint>

// Tracks mutually exclusive capture and playback for half-duplex audio.
// This type does not synchronize callers or stop active audio; it only
// validates and records transitions.

namespace stackchan::domain {

enum class AudioDirectionState : std::uint8_t {
  idle,       // neither direction is running
  capturing,  // recording
  playing,    // playing back
};

// The answer from begin_capture and begin_playback.
enum class AudioTransition : std::uint8_t {
  proceed,        // go ahead; the state has already changed
  already_active, // that direction is already running; nothing to do
  denied,         // the other direction is running; stop it first
};

class AudioDirection {
 public:
  AudioDirection() noexcept = default;

  // Ask to start recording. Denied while playing; playback is not stopped.
  [[nodiscard]] AudioTransition begin_capture() noexcept;

  // Ask to start playing. Denied while recording.
  [[nodiscard]] AudioTransition begin_playback() noexcept;

  // Stopped. Does nothing if that direction was not running, which matches
  // how stop_capture and stop_playback behave.
  void end_capture() noexcept;
  void end_playback() noexcept;

  [[nodiscard]] AudioDirectionState state() const noexcept { return state_; }
  [[nodiscard]] bool idle() const noexcept { return state_ == AudioDirectionState::idle; }
  [[nodiscard]] bool capturing() const noexcept {
    return state_ == AudioDirectionState::capturing;
  }
  [[nodiscard]] bool playing() const noexcept {
    return state_ == AudioDirectionState::playing;
  }

 private:
  AudioDirectionState state_ = AudioDirectionState::idle;
};

[[nodiscard]] const char* to_string(AudioDirectionState state) noexcept;
[[nodiscard]] const char* to_string(AudioTransition transition) noexcept;

}  // namespace stackchan::domain
