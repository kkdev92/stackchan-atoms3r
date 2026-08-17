#include "stackchan/runtime/deadline.hpp"

namespace stackchan::runtime {

Deadline Deadline::after(Millis now, Millis timeout) noexcept {
  // Wrapping past the end of the range is expected and harmless; remaining()
  // works from differences, which survive the wrap.
  return Deadline{now + timeout, true};
}

bool Deadline::expired(Millis now) const noexcept {
  return bounded_ && remaining(now) == 0;
}

Millis Deadline::remaining(Millis now) const noexcept {
  if (!bounded_) {
    return kUnbounded;
  }

  // Never compare two absolute instants: the comparison inverts across the
  // wrap. Unsigned subtraction keeps the difference correct, so decide on
  // that instead.
  //
  // Once the deadline has passed the difference is a very large number.
  // Half the range separates "not yet" from "already past" — half is about
  // 24.8 days, ample for any wait here. Both operands are Millis, so the
  // difference fits in Millis too.
  const auto delta = static_cast<Millis>(expires_at_ - now);
  constexpr Millis kHalf = UINT32_MAX / 2;
  return delta > kHalf ? 0 : delta;
}

Deadline Deadline::earlier_of(Deadline other, Millis now) const noexcept {
  if (!bounded_) {
    return other;
  }
  if (!other.bounded_) {
    return *this;
  }
  // Compare time remaining; comparing the instants would be wrong across
  // the wrap.
  return other.remaining(now) < remaining(now) ? other : *this;
}

}  // namespace stackchan::runtime
