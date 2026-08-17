#pragma once

#include <cstdint>

// A deadline: "how long to wait", carried around as a value.
//
// Why it exists
// -------------
// A wait with no upper bound is a hang waiting to happen: something that
// never arrives, and a device that never recovers. Routing every wait
// through this type makes that impossible to write by accident. It is
// invariant 4 of the design principles.
//
// If you add a call that waits, it takes a Deadline. There is deliberately
// no overload that does not.
//
// A deadline holds an absolute instant, not a relative duration, so that
// time does not stretch as a call descends. If A calls B with three seconds
// left and B calls C, then C must not get longer than A has remaining.
// Passing a duration restarts the clock at every level.
//
// Wrap-around
// -----------
// The monotonic clock is 32-bit milliseconds and returns to zero after
// about 49.7 days. Comparing two absolute instants inverts the moment the
// wrap is crossed. So every comparison is reduced to "how far from now",
// because unsigned subtraction stays correct across the wrap. This holds as
// long as the difference stays under half the range — about 24.8 days,
// which is ample for a wait.

namespace stackchan::runtime {

using Millis = std::uint32_t;

class Deadline {
 public:
  // No deadline; expired() never becomes true. It exists to express "no
  // bound has been set yet" in the type. Using it for a real wait is only
  // acceptable when nothing external is being waited on.
  [[nodiscard]] static Deadline never() noexcept { return Deadline{}; }

  // Expires timeout milliseconds after now. A timeout of 0 has already
  // expired.
  [[nodiscard]] static Deadline after(Millis now, Millis timeout) noexcept;

  // Whether a bound was set. False means never().
  [[nodiscard]] bool bounded() const noexcept { return bounded_; }

  [[nodiscard]] bool expired(Millis now) const noexcept;

  // Time left. Zero once expired, kUnbounded if there is no deadline.
  [[nodiscard]] Millis remaining(Millis now) const noexcept;

  // Take whichever expires first. Use it when passing a call downwards:
  //
  //   auto child = parent.earlier_of(Deadline::after(now, 3000), now);
  //
  // A child can then never outlast its parent, so the bound propagates.
  [[nodiscard]] Deadline earlier_of(Deadline other, Millis now) const noexcept;

  // What remaining() returns when there is no deadline.
  static constexpr Millis kUnbounded = UINT32_MAX;

 private:
  Deadline() noexcept = default;
  Deadline(Millis expires_at, bool bounded) noexcept
      : expires_at_(expires_at), bounded_(bounded) {}

  Millis expires_at_ = 0;
  bool bounded_ = false;
};

}  // namespace stackchan::runtime
