#pragma once

#include <cstdint>

// Turning the raw state of a physical button into usable edges.
//
// A mechanical switch does not close cleanly: for a few milliseconds the
// contact bounces, opening and closing repeatedly. Read the pin directly
// and one press looks like several. This settles the value, reporting a
// change only once the reading has held steady.
//
// No hardware is touched, so it runs on the host. Converting the pin's
// polarity — the button on this board reads low when pressed — is the
// caller's job.

namespace stackchan::domain {

enum class ButtonEdge : std::uint8_t {
  none,
  pressed,
  released,
};

class ButtonDebouncer {
 public:
  // stable_ms: how long a reading must hold before it is believed. Too
  //   short lets bounce through; too long makes the button feel sluggish.
  explicit ButtonDebouncer(std::uint32_t stable_ms = 20) noexcept;

  // Feed the raw reading and the current time; pressed is true when the
  // button is down. An edge is reported only when the settled state
  // changes.
  //
  // The first call never reports an edge: a button already held at startup
  // was not just pressed.
  [[nodiscard]] ButtonEdge update(bool pressed, std::uint32_t now_ms) noexcept;

  // The settled state.
  [[nodiscard]] bool is_pressed() const noexcept { return stable_; }

  // How long the button has been held. Zero when it is not down.
  [[nodiscard]] std::uint32_t held_ms(std::uint32_t now_ms) const noexcept;

  // How long the last completed press lasted. Zero until one has finished.
  //
  // Read this when a release edge arrives, because by then held_ms() is
  // already zero.
  //
  // Measured between the times the contact actually moved, not between the
  // times the readings settled — otherwise the debounce interval would be
  // added to every press.
  [[nodiscard]] std::uint32_t last_press_ms() const noexcept {
    return last_press_ms_;
  }

 private:
  std::uint32_t stable_ms_;
  bool stable_ = false;      // the settled state
  bool candidate_ = false;   // a reading not yet believed
  std::uint32_t candidate_since_ = 0;
  std::uint32_t stable_since_ = 0;
  std::uint32_t last_press_ms_ = 0;
  bool started_ = false;
};

}  // namespace stackchan::domain
