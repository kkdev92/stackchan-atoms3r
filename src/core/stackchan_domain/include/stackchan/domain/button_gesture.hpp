#pragma once

#include <cstdint>

#include "stackchan/domain/button.hpp"

// Turning presses of the button into meaningful actions.
//
// There is only one button on this device, so it carries four actions: one
// press starts a conversation, two reports what hardware is attached, three
// reports the current connection, and a long press stops whatever is being
// said.
//
// How the counting works
// ----------------------
//   - a press is counted the moment the button goes down, not when it is
//     released, so the device reacts while a finger is still moving
//   - the window is measured from the **first** press, not the most recent
//     one, so holding a stream of presses cannot extend it indefinitely
//   - when the window closes, the accumulated count becomes one gesture
//
// This is all decision and no hardware, so it is tested on the host. The
// caller supplies the current time.
//
// A long press discards the count
// -------------------------------
// Otherwise pressing and holding would first register as a press and then
// as a hold, and the device would start a conversation on the way to being
// told to be quiet.

namespace stackchan::domain {

enum class GestureKind : std::uint8_t {
  none,
  // Held past the threshold. Emitted once, while the button is still down.
  hold,
  // The window closed; clicks holds how many presses it contained.
  clicks,
};

struct Gesture {
  GestureKind kind = GestureKind::none;
  // Meaningful only when kind is clicks.
  std::uint8_t clicks = 0;
};

class ButtonGesture {
 public:
  // multi_click_window_ms: how long presses keep accumulating into one
  //   gesture. Too short and a fast double press becomes two singles.
  // hold_ms: how long the button must stay down to count as a long press.
  explicit ButtonGesture(std::uint32_t multi_click_window_ms = 1000,
                         std::uint32_t hold_ms = 500) noexcept;

  // Feed the button edges and the current time. Call this even when there
  // is no edge: both closing the window and reaching a long press are
  // decided by time passing, not by the button changing.
  [[nodiscard]] Gesture update(ButtonEdge edge, std::uint32_t now_ms) noexcept;

  // Presses accumulated so far, before the window closes.
  [[nodiscard]] std::uint8_t pending_clicks() const noexcept { return clicks_; }

 private:
  std::uint32_t window_ms_;
  std::uint32_t hold_ms_;

  bool pressed_ = false;
  bool hold_reported_ = false;
  std::uint32_t pressed_since_ = 0;

  std::uint8_t clicks_ = 0;
  std::uint32_t window_since_ = 0;
};

}  // namespace stackchan::domain
