#include "stackchan/domain/button.hpp"

namespace stackchan::domain {
namespace {

// Unsigned subtraction wraps at 2^32, so elapsed time stays correct even
// when the millisecond counter wraps, which it does about every 49.7 days.
[[nodiscard]] std::uint32_t elapsed(std::uint32_t from, std::uint32_t to) noexcept {
  return to - from;
}

}  // namespace

ButtonDebouncer::ButtonDebouncer(std::uint32_t stable_ms) noexcept
    : stable_ms_(stable_ms) {}

ButtonEdge ButtonDebouncer::update(bool pressed, std::uint32_t now_ms) noexcept {
  if (!started_) {
  // Adopt the first reading as-is and report no edge: a button already held
  // at power-on was not just pressed.
    started_ = true;
    stable_ = pressed;
    candidate_ = pressed;
    candidate_since_ = now_ms;
    stable_since_ = now_ms;
    return ButtonEdge::none;
  }

  if (pressed != candidate_) {
    // The reading changed, so start timing again from here.
    candidate_ = pressed;
    candidate_since_ = now_ms;
    return ButtonEdge::none;
  }

  if (candidate_ == stable_) {
    return ButtonEdge::none;  // same as the settled value; nothing happened
  }

  if (elapsed(candidate_since_, now_ms) < stable_ms_) {
    return ButtonEdge::none;  // not settled yet
  }

  if (!candidate_) {
    // Released. Record how long the press lasted.
    //
    // Measured between the times the contact moved, not the times the
    // readings settled — otherwise every press would appear longer by the
    // debounce interval.
    last_press_ms_ = elapsed(stable_since_, candidate_since_);
  }

  stable_ = candidate_;
  stable_since_ = now_ms;
  return stable_ ? ButtonEdge::pressed : ButtonEdge::released;
}

std::uint32_t ButtonDebouncer::held_ms(std::uint32_t now_ms) const noexcept {
  if (!stable_) {
    return 0;
  }
  return elapsed(stable_since_, now_ms);
}

}  // namespace stackchan::domain
