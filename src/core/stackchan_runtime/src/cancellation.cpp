#include "stackchan/runtime/cancellation.hpp"

namespace stackchan::runtime {
namespace {

// What none() points at. Nothing can write to it. It is a function-local
// static so that nothing depends on initialisation order across translation
// units.
[[nodiscard]] const std::atomic<CancelReason>& never_cancelled() noexcept {
  static const std::atomic<CancelReason> value{CancelReason::none};
  return value;
}

}  // namespace

CancellationToken CancellationToken::none() noexcept {
  return CancellationToken{&never_cancelled()};
}

void CancellationSource::cancel(CancelReason reason) noexcept {
  if (reason == CancelReason::none) {
    return;
  }

  // Keep the graver reason. The compare-exchange loop is what stops a
  // concurrent call, from another task or an interrupt, replacing it with a
  // lighter one.
  CancelReason current = reason_.load(std::memory_order_acquire);
  while (static_cast<std::uint8_t>(reason) > static_cast<std::uint8_t>(current)) {
    if (reason_.compare_exchange_weak(current, reason, std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
      return;
    }
    // On failure current holds the latest value, so compare gravity again.
  }
}

void CancellationSource::reset() noexcept {
  // An emergency stop and a shutdown both survive reset. Otherwise the
  // reset at the end of each conversation would silently release them and
  // let the next one start.
  CancelReason current = reason_.load(std::memory_order_acquire);
  while (current != CancelReason::emergency_stop &&
         current != CancelReason::shutdown && current != CancelReason::none) {
    if (reason_.compare_exchange_weak(current, CancelReason::none,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire)) {
      return;
    }
  }
}

void CancellationSource::clear_emergency() noexcept {
  // Release only the emergency stop; do nothing during a shutdown.
  CancelReason expected = CancelReason::emergency_stop;
  reason_.compare_exchange_strong(expected, CancelReason::none,
                                  std::memory_order_acq_rel,
                                  std::memory_order_acquire);
}

}  // namespace stackchan::runtime
