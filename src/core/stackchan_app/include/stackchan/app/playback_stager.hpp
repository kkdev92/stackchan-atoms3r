#pragma once

#include <cstddef>
#include <cstdint>

#include "stackchan/domain/expression.hpp"
#include "stackchan/ports/audio.hpp"

// Buffering speech: hold a sentence until it is complete, then play all of
// it at once.
//
// Why not just play what arrives
// ------------------------------
// The reply audio arrives in small pieces at roughly the speed it is
// spoken. Hand each piece straight to the audio hardware and its buffer
// runs dry between them, silence gets inserted, and the result crackles.
// Enlarging that buffer only delays the problem: if the sender is ever
// slower than real time, it empties eventually.
//
// Holding a whole sentence removes the failure rather than postponing it.
// Playback starts only when there is enough to finish.
//
// The expression must not run ahead of the sound
// ----------------------------------------------
// If the face changed the moment a sentence's marker arrived, it would be
// wearing the next sentence's expression while the previous one is still
// being spoken. So expression_to_speak() reports the expression of the
// audio about to play, and the next one is promoted only after that audio
// has been handed over.
//
// The calling contract
// --------------------
// This type decides; it never touches the audio hardware itself.
//
//   begin_sentence(next) returns n > 0:
//     play n samples from staged_samples() wearing expression_to_speak(),
//     then call flushed(now) once they are gone.
//   accept(samples, count) returns less than count:
//     the buffer is full. Play what is staged, call flushed(), and offer
//     the remainder again.
//   finish(true) returns n > 0:
//     play the final sentence the same way. finish(false) discards what is
//     left, because speech resuming after a cancellation is unnerving.
//
// A new sentence cannot be mixed in before flushed() is called: accept()
// returns 0 until then, which is what keeps sentence boundaries meaningful.
//
// Time
// ----
// The audio hardware keeps playing for a while after the last write, so
// stopping immediately clips the end of the final sentence. remaining_ms()
// answers how much is still expected to be sounding.
//
// Memory
// ------
// The buffer belongs to the caller and never grows.

namespace stackchan::app {

class PlaybackStager {
 public:
  // stage / capacity_samples: where sentences accumulate. Not owned.
  // sink_depth_ms: how much audio the hardware still holds after a write
  //   has been accepted. Keep this in step with the audio configuration.
  PlaybackStager(ports::Sample* stage, std::size_t capacity_samples,
                 std::uint32_t sink_depth_ms) noexcept
      : stage_(stage), capacity_(stage == nullptr ? 0 : capacity_samples),
        sink_depth_ms_(sink_depth_ms) {}

  PlaybackStager(const PlaybackStager&) = delete;
  PlaybackStager& operator=(const PlaybackStager&) = delete;
  PlaybackStager(PlaybackStager&&) = delete;
  PlaybackStager& operator=(PlaybackStager&&) = delete;
  ~PlaybackStager() = default;

  // A new sentence begins. Returns how many samples must be played first,
  // or 0 if there are none — in which case the expression changes at once,
  // and otherwise after flushed().
  [[nodiscard]] std::size_t begin_sentence(domain::Expression next) noexcept;

  // Accumulate audio. Returns how many samples were taken; fewer than
  // asked means the buffer is full (see the calling contract above).
  [[nodiscard]] std::size_t accept(const ports::Sample* samples,
                                   std::size_t count) noexcept;

  // End of stream. When completed, returns what is left to play; otherwise
  // discards it and returns 0.
  [[nodiscard]] std::size_t finish(bool completed) noexcept;

  // The staged samples have been played. Promotes the next expression if
  // one is waiting.
  void flushed(std::uint32_t now_ms) noexcept;

  // Reset for the next conversation.
  void reset() noexcept;

  [[nodiscard]] const ports::Sample* staged_samples() const noexcept { return stage_; }
  [[nodiscard]] std::size_t staged_count() const noexcept { return staged_; }

  // The expression belonging to the audio currently staged.
  [[nodiscard]] domain::Expression expression_to_speak() const noexcept {
    return speaking_;
  }

  // How much audio is still expected to be sounding. Used when winding a
  // conversation down, so the end is not clipped.
  [[nodiscard]] std::uint32_t remaining_ms(std::uint32_t now_ms) const noexcept;

 private:
  ports::Sample* stage_;
  std::size_t capacity_;
  std::uint32_t sink_depth_ms_;

  std::size_t staged_ = 0;
  domain::Expression speaking_ = domain::Expression::neutral;
  domain::Expression pending_ = domain::Expression::neutral;
  bool has_pending_ = false;

  std::uint32_t until_ms_ = 0;
  bool spoke_ = false;
};

}  // namespace stackchan::app
