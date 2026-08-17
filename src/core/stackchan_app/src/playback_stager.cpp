#include "stackchan/app/playback_stager.hpp"

#include <cstring>

namespace stackchan::app {

std::size_t PlaybackStager::begin_sentence(domain::Expression next) noexcept {
  if (staged_ == 0) {
    // Nothing staged, so the expression can change immediately: there is
    // no earlier sentence waiting to be played out.
    speaking_ = next;
    has_pending_ = false;
    return 0;
  }
  // Audio from the previous sentence is still staged. Play it out under the
  // current expression before switching, or the face runs a sentence ahead
  // of the sound.
  pending_ = next;
  has_pending_ = true;
  return staged_;
}

std::size_t PlaybackStager::accept(const ports::Sample* samples,
                                   std::size_t count) noexcept {
  if (samples == nullptr || count == 0 || capacity_ == 0) {
    return 0;
  }
  // Until the boundary has been flushed, do not mix in the next sentence's
  // audio: that would erase where one sentence ends and break the point at
  // which the expression changes.
  if (has_pending_) {
    return 0;
  }
  const std::size_t room = capacity_ - staged_;
  const std::size_t take = count < room ? count : room;
  if (take == 0) {
    return 0;  // full; play the staged audio, then offer this again
  }
  std::memcpy(stage_ + staged_, samples, take * sizeof(ports::Sample));
  staged_ += take;
  return take;
}

std::size_t PlaybackStager::finish(bool completed) noexcept {
  if (!completed) {
    // Speech resuming after a cancellation is unnerving, so discard it.
    staged_ = 0;
    has_pending_ = false;
    return 0;
  }
  // The last sentence has no next sentence to mark its boundary, so it is
  // played out here.
  return staged_;
}

void PlaybackStager::flushed(std::uint32_t now_ms) noexcept {
  staged_ = 0;
  // The caller's write blocks until the hardware accepts the samples, so by
  // the time it returns, only the hardware's own buffer is still to sound.
  until_ms_ = now_ms + sink_depth_ms_;
  spoke_ = true;
  if (has_pending_) {
    speaking_ = pending_;
    has_pending_ = false;
  }
}

void PlaybackStager::reset() noexcept {
  staged_ = 0;
  speaking_ = domain::Expression::neutral;
  pending_ = domain::Expression::neutral;
  has_pending_ = false;
  until_ms_ = 0;
  spoke_ = false;
}

std::uint32_t PlaybackStager::remaining_ms(std::uint32_t now_ms) const noexcept {
  if (!spoke_) {
    return 0;
  }
  // Compared as a **signed** difference so the wrap is handled. Subtracting
  // unsigned turns "one millisecond past" into an enormous positive number.
  const auto left = static_cast<std::int32_t>(until_ms_ - now_ms);
  return left > 0 ? static_cast<std::uint32_t>(left) : 0;
}

}  // namespace stackchan::app
