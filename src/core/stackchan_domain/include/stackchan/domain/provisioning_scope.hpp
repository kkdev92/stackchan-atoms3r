#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Which route may configure the device.
//
// The problem
// -----------
// Configuration means writing Wi-Fi credentials, and those are persisted.
// Anyone who can reach that endpoint can point the device at a network of
// their choosing, and it will go there on the next restart. On a home LAN
// that is everyone on the LAN.
//
// The intent has always been narrower than that: whoever can read the
// passphrase off the device's own screen may configure it. Physical sight
// of the display is the credential. So the rule is:
//
//   **Accept configuration only from requests that arrived on the access
//   point's own interface.**
//
// Why this lives in core
// ----------------------
// It cannot be solved by configuring the HTTP server: there is no option
// for restricting which address it listens on. The check has to happen
// inside the handler — and it reduces to a pure function from facts to a
// yes or no, which is worth testing. Obtaining the facts from the socket is
// the platform's job.
//
// When in doubt, refuse
// ---------------------
// A request whose arrival interface cannot be determined is rejected.
// Configuration is needed for a few minutes at setup; refusing wrongly
// costs a retry, while accepting wrongly hands over the network.

namespace stackchan::domain {

// An IPv4 address, as a plain value so that no networking type reaches
// core. octets is in written order: 192.168.4.1 becomes {192,168,4,1}.
struct Ipv4 {
  std::array<std::uint8_t, 4> octets{};

  [[nodiscard]] static constexpr Ipv4 of(std::uint8_t a, std::uint8_t b, std::uint8_t c,
                                         std::uint8_t d) noexcept {
    return Ipv4{{a, b, c, d}};
  }

  [[nodiscard]] bool operator==(const Ipv4& other) const noexcept {
    return octets == other.octets;
  }
  [[nodiscard]] bool operator!=(const Ipv4& other) const noexcept {
    return !(*this == other);
  }
};

// Whether a configuration request may be accepted.
//
//   access_point_up       whether the access point is currently broadcasting
//   access_point_address  the address of the access point's own interface.
//                         The platform looks this up each time rather than
//                         assuming 192.168.4.1, so the rule stays correct
//                         if that default is ever changed
//   request_local_address the interface the request arrived on. Pass
//                         nullopt if it could not be determined
//
// Everything is refused while the access point is down, which keeps "can be
// configured" and "is showing a passphrase on screen" the same state.
[[nodiscard]] bool provisioning_allowed(
    bool access_point_up, const Ipv4& access_point_address,
    const std::optional<Ipv4>& request_local_address) noexcept;

}  // namespace stackchan::domain
