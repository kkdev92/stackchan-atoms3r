#include "stackchan/domain/button_gesture.hpp"

namespace stackchan::domain {
namespace {

// Correct across the counter wrapping, for the same reason as elsewhere.
[[nodiscard]] std::uint32_t elapsed(std::uint32_t from, std::uint32_t to) noexcept {
  return to - from;
}

}  // namespace

ButtonGesture::ButtonGesture(std::uint32_t multi_click_window_ms,
                             std::uint32_t hold_ms) noexcept
    : window_ms_(multi_click_window_ms), hold_ms_(hold_ms) {}

Gesture ButtonGesture::update(ButtonEdge edge, std::uint32_t now_ms) noexcept {
  if (edge == ButtonEdge::pressed) {
    pressed_ = true;
    hold_reported_ = false;
    pressed_since_ = now_ms;
    if (clicks_ == 0) {
      // The window is measured from the first press.
      window_since_ = now_ms;
    }
    if (clicks_ < 255) {
      ++clicks_;
    }
  } else if (edge == ButtonEdge::released) {
    pressed_ = false;
  }

  // A hold takes precedence, is reported while the button is still down,
  // and discards the accumulated presses.
  if (pressed_ && !hold_reported_ && elapsed(pressed_since_, now_ms) >= hold_ms_) {
    hold_reported_ = true;
    clicks_ = 0;
    return Gesture{GestureKind::hold, 0};
  }

  if (clicks_ > 0 && elapsed(window_since_, now_ms) >= window_ms_) {
    const Gesture gesture{GestureKind::clicks, clicks_};
    clicks_ = 0;
    return gesture;
  }

  return Gesture{};
}

}  // namespace stackchan::domain
