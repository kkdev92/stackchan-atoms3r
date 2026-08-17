#pragma once

#include <cstdint>

// How the network is reconnected.
//
// What this solves
// ----------------
// Retrying a failed connection at a fixed interval creates unnecessary
// traffic: if
// the access point is off for an hour, the radio spends that hour shouting
// at nothing and interfering with everything else on the band.
//
// So the interval doubles with each failure, up to a cap, and returns to
// normal as soon as a connection succeeds.
//
// Why this lives in core
// ----------------------
// Computing the interval, and deciding when to raise an access point, need
// no radio. They are exactly the parts worth verifying without hardware.
// Calling ESP-IDF is platform/stackchan_net's job.

namespace stackchan::domain {

enum class NetworkPhase : std::uint8_t {
  // No credentials. Do not connect; accept configuration instead.
  unprovisioned,
  // Connecting.
  connecting,
  // Connected.
  connected,
  // Failed, and waiting out the interval before trying again.
  backoff,
};

class NetworkPolicy {
 public:
  // first_backoff_ms:    how long to wait after the first failure
  // max_backoff_ms:      never wait longer than this
  // ap_after_failures:   consecutive failures before raising an access point
  //
  // The cap exists because an unbounded interval — after a long power cut,
  // say — would mean not noticing that the network came back.
  //
  // explicit because every argument has a default, which would otherwise
  // make this a conversion from std::uint32_t: a stray number would turn
  // itself into a policy wherever one is expected.
  explicit NetworkPolicy(std::uint32_t first_backoff_ms = 1000,
                         std::uint32_t max_backoff_ms = 60000,
                         std::uint8_t ap_after_failures = 3) noexcept;

  [[nodiscard]] NetworkPhase phase() const noexcept { return phase_; }
  [[nodiscard]] std::uint8_t consecutive_failures() const noexcept { return failures_; }

  // Report whether credentials exist. Without them the phase stays
  // unprovisioned.
  //
  // Reporting true again resets the failure count and the interval: new
  // credentials mean a different peer, so the previous peer's backoff must
  // not carry over. The connecting and connected phases are left alone.
  void set_provisioned(bool provisioned) noexcept;

  // Report how many clients the access point has.
  //
  // While any are connected, reconnection attempts with credentials already
  // known to fail are suppressed. Reconnecting moves the radio to another
  // channel, which disconnects them — so the very act of retrying knocks
  // the person off the setup page while they are trying to submit it.
  //
  // The exception is the first attempt right after new credentials arrive
  // (zero failures), because whoever entered them is watching the screen
  // for the outcome.
  void set_guest_count(std::uint8_t guests) noexcept { guests_ = guests; }

  // A connection attempt has started.
  void on_attempt_started(std::uint32_t now_ms) noexcept;

  // Connected. Also resets the failure count.
  void on_connected() noexcept;

  // Failed, or the connection dropped.
  void on_failed(std::uint32_t now_ms) noexcept;

  // Whether to attempt a reconnection now. False while backing off.
  [[nodiscard]] bool should_attempt(std::uint32_t now_ms) const noexcept;

  // Time until the next attempt; zero if one may start immediately.
  [[nodiscard]] std::uint32_t wait_remaining_ms(std::uint32_t now_ms) const noexcept;

  // The current interval, doubling with each failure.
  [[nodiscard]] std::uint32_t current_backoff_ms() const noexcept { return backoff_ms_; }

  // Whether an access point should be up.
  //
  // True from the start when there are no credentials, so a unit that
  // cannot connect is never left with no way to configure it.
  [[nodiscard]] bool should_open_access_point() const noexcept;

 private:
  std::uint32_t first_backoff_ms_;
  std::uint32_t max_backoff_ms_;
  std::uint8_t ap_after_failures_;

  NetworkPhase phase_ = NetworkPhase::unprovisioned;
  bool provisioned_ = false;
  std::uint8_t guests_ = 0;
  std::uint8_t failures_ = 0;
  std::uint32_t backoff_ms_ = 0;
  std::uint32_t backoff_since_ = 0;
};

}  // namespace stackchan::domain
