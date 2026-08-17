#include "stackchan/domain/audio_direction.hpp"

namespace stackchan::domain {

AudioTransition AudioDirection::begin_capture() noexcept {
  switch (state_) {
    case AudioDirectionState::capturing:
      return AudioTransition::already_active;
    case AudioDirectionState::playing:
      // Playback is not stopped here. Stopping it is the caller's explicit
      // decision to make.
      return AudioTransition::denied;
    case AudioDirectionState::idle:
      break;
  }
  state_ = AudioDirectionState::capturing;
  return AudioTransition::proceed;
}

AudioTransition AudioDirection::begin_playback() noexcept {
  switch (state_) {
    case AudioDirectionState::playing:
      return AudioTransition::already_active;
    case AudioDirectionState::capturing:
      return AudioTransition::denied;
    case AudioDirectionState::idle:
      break;
  }
  state_ = AudioDirectionState::playing;
  return AudioTransition::proceed;
}

void AudioDirection::end_capture() noexcept {
  // end_capture arriving during playback must not disturb it. The state is
  // only reset when the direction matches.
  if (state_ == AudioDirectionState::capturing) {
    state_ = AudioDirectionState::idle;
  }
}

void AudioDirection::end_playback() noexcept {
  if (state_ == AudioDirectionState::playing) {
    state_ = AudioDirectionState::idle;
  }
}

const char* to_string(AudioDirectionState state) noexcept {
  switch (state) {
    case AudioDirectionState::idle:
      return "idle";
    case AudioDirectionState::capturing:
      return "capturing";
    case AudioDirectionState::playing:
      return "playing";
  }
  return "unknown";
}

const char* to_string(AudioTransition transition) noexcept {
  switch (transition) {
    case AudioTransition::proceed:
      return "proceed";
    case AudioTransition::already_active:
      return "already_active";
    case AudioTransition::denied:
      return "denied";
  }
  return "unknown";
}

}  // namespace stackchan::domain
