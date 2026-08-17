#pragma once

#include <atomic>
#include <cstdint>

// Cancellation: stopping work in progress from outside it.
//
// Why it exists
// -------------
// So there is no state in which an emergency stop fails to take effect. A
// long-running conversation must not be able to block the request that
// stops it.
//
// Cancellation is not delivered through a queue. Queues fill up, and a
// cancellation has to arrive even when everything else is backed up — the
// thing being cancelled is usually the thing that is stuck. Instead there
// is one shared flag that waiters check for themselves. Nothing can block
// it, and it reaches a waiter however deeply nested it is.
//
// How reasons combine
// -------------------
// The graver reason wins, not the earlier one. An emergency stop during an
// ordinary cancellation becomes an emergency stop; a timeout during an
// emergency stop leaves the emergency stop in place. Recovery differs by
// reason, so losing the gravest one would be dangerous — an emergency stop
// demands an explicit release.

namespace stackchan::runtime {

// Ordered by gravity. That order is what decides which reason survives, so
// a new value has to be inserted at the right position.
enum class CancelReason : std::uint8_t {
  none = 0,
  timeout = 1,         // a deadline passed
  requested = 2,       // an ordinary stop, from the API or the button
  shutdown = 3,        // shutting down; nothing resumes
  emergency_stop = 4,  // emergency stop; needs an explicit release
};

// The reading end. Cheap to pass around by value.
//
// It must not outlive its source, or it refers to nothing. In practice the
// conversation task owns the source and hands tokens downwards.
class CancellationToken {
 public:
  // A token that is never cancelled, for callees that do not support
  // cancellation.
  [[nodiscard]] static CancellationToken none() noexcept;

  [[nodiscard]] bool cancelled() const noexcept {
    return reason() != CancelReason::none;
  }

  [[nodiscard]] CancelReason reason() const noexcept {
    return reason_->load(std::memory_order_acquire);
  }

  // For places that care only about the emergency stop, because recovery
  // depends on it.
  [[nodiscard]] bool emergency() const noexcept {
    return reason() == CancelReason::emergency_stop;
  }

 private:
  friend class CancellationSource;
  explicit CancellationToken(const std::atomic<CancelReason>* reason) noexcept
      : reason_(reason) {}

  const std::atomic<CancelReason>* reason_;
};

// The signalling end.
class CancellationSource {
 public:
  CancellationSource() = default;
  ~CancellationSource() = default;

  // Exactly one source per thing being cancelled. A copy would leave it
  // unclear which one a token observes.
  CancellationSource(const CancellationSource&) = delete;
  CancellationSource& operator=(const CancellationSource&) = delete;
  CancellationSource(CancellationSource&&) = delete;
  CancellationSource& operator=(CancellationSource&&) = delete;

  // Cancel. A graver reason already in place is not overwritten. Safe to
  // call repeatedly, and safe to call from an interrupt.
  void cancel(CancelReason reason) noexcept;

  [[nodiscard]] bool cancelled() const noexcept {
    return reason() != CancelReason::none;
  }

  [[nodiscard]] CancelReason reason() const noexcept {
    return reason_.load(std::memory_order_acquire);
  }

  // Clear, ready for the next piece of work.
  //
  // An emergency stop survives reset(); releasing it requires
  // clear_emergency(). Otherwise the reset at the end of every conversation
  // would quietly release it.
  void reset() noexcept;

  // Release the emergency stop. Called only from the estop.clear command.
  void clear_emergency() noexcept;

  [[nodiscard]] CancellationToken token() const noexcept {
    return CancellationToken{&reason_};
  }

 private:
  std::atomic<CancelReason> reason_{CancelReason::none};
};

}  // namespace stackchan::runtime
