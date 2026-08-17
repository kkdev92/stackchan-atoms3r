#pragma once

#include <cstdint>

#include "stackchan/domain/button_gesture.hpp"

// The user button.
//
// The decisions — debouncing, and reading presses as gestures — live in
// core and are covered by host tests. What is here is reading the GPIO and
// feeding those two.
//
// The button is active low. The board pulls it up, and the internal pull-up
// is enabled as well: harmless twice over, and it stops a floating pin from
// being read as permanently pressed.

namespace stackchan::input {

// Configure the GPIO as an input. Called once at boot.
[[nodiscard]] bool begin();

// Call every iteration. Returns anything but none only when a gesture has
// been recognised.
[[nodiscard]] domain::Gesture poll(std::uint32_t now_ms);

}  // namespace stackchan::input
