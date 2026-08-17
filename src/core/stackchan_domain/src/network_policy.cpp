#include "stackchan/domain/network_policy.hpp"

namespace stackchan::domain {
namespace {

[[nodiscard]] std::uint32_t elapsed(std::uint32_t from, std::uint32_t to) noexcept {
  return to - from;  // unsigned, so the difference survives the wrap
}

}  // namespace

NetworkPolicy::NetworkPolicy(std::uint32_t first_backoff_ms,
                             std::uint32_t max_backoff_ms,
                             std::uint8_t ap_after_failures) noexcept
    : first_backoff_ms_(first_backoff_ms == 0 ? 1 : first_backoff_ms),
      max_backoff_ms_(max_backoff_ms),
      ap_after_failures_(ap_after_failures) {}

void NetworkPolicy::set_provisioned(bool provisioned) noexcept {
  provisioned_ = provisioned;
  if (!provisioned) {
    phase_ = NetworkPhase::unprovisioned;
    failures_ = 0;
    backoff_ms_ = 0;
    return;
  }

  // Credentials arrived, or were replaced. That means a different network,
  // so the failure count and the interval start again — otherwise someone
  // correcting a typo would wait out the backoff earned by the typo.
  failures_ = 0;
  backoff_ms_ = 0;
  backoff_since_ = 0;

  // While an attempt is in flight, wait for its outcome: the radio can only
  // be asked for one connection at a time. While connected, stay connected;
  // new credentials take effect at the next disconnection.
  if (phase_ != NetworkPhase::connecting && phase_ != NetworkPhase::connected) {
    phase_ = NetworkPhase::backoff;
  }
}

void NetworkPolicy::on_attempt_started(std::uint32_t now_ms) noexcept {
  if (!provisioned_) {
    return;
  }
  (void)now_ms;
  phase_ = NetworkPhase::connecting;
}

void NetworkPolicy::on_connected() noexcept {
  phase_ = NetworkPhase::connected;
  // Start counting again, and put the interval back to its shortest.
  failures_ = 0;
  backoff_ms_ = 0;
}

void NetworkPolicy::on_failed(std::uint32_t now_ms) noexcept {
  if (!provisioned_) {
    return;
  }
  if (failures_ < UINT8_MAX) {
    ++failures_;
  }

  // Double the interval with each failure, up to the cap. The cap is what
  // keeps the device from taking hours to notice the network came back.
  if (backoff_ms_ == 0) {
    backoff_ms_ = first_backoff_ms_;
  } else if (backoff_ms_ < max_backoff_ms_) {
    const std::uint32_t doubled = backoff_ms_ * 2;
    // Compare against the cap before multiplying, so the multiplication
    // cannot overflow.
    backoff_ms_ = (doubled < backoff_ms_ || doubled > max_backoff_ms_) ? max_backoff_ms_
                                                                      : doubled;
  }

  backoff_since_ = now_ms;
  phase_ = NetworkPhase::backoff;
}

bool NetworkPolicy::should_attempt(std::uint32_t now_ms) const noexcept {
  if (!provisioned_ || phase_ == NetworkPhase::connecting ||
      phase_ == NetworkPhase::connected) {
    return false;
  }
  // While clients are attached, do not retry credentials already known to
  // fail. Zero failures means they were only just entered, so that one
  // attempt is allowed through.
  if (guests_ > 0 && failures_ > 0) {
    return false;
  }
  return elapsed(backoff_since_, now_ms) >= backoff_ms_;
}

std::uint32_t NetworkPolicy::wait_remaining_ms(std::uint32_t now_ms) const noexcept {
  if (should_attempt(now_ms)) {
    return 0;
  }
  const std::uint32_t waited = elapsed(backoff_since_, now_ms);
  return waited >= backoff_ms_ ? 0 : backoff_ms_ - waited;
}

bool NetworkPolicy::should_open_access_point() const noexcept {
  // With no credentials, raise it immediately: a device that cannot connect
  // must never be left with no way to configure it.
  if (!provisioned_) {
    return true;
  }
  // Not needed while connected.
  if (phase_ == NetworkPhase::connected) {
    return false;
  }
  return failures_ >= ap_after_failures_;
}

}  // namespace stackchan::domain
