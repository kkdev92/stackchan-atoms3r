#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/domain/network_policy.hpp"

// Wi-Fi. Connect when credentials are stored; otherwise raise an access
// point so they can be entered.
//
// Where things are kept
// ---------------------
//   credentials   NVS namespace "wifi", keys "ssid" and "pass"
//   AP name       "STACKCHAN-" followed by six hex digits derived from the
//                 chip's MAC address, so two units nearby are
//                 distinguishable
//   mode          station mode is used with stored credentials; station and
//                 access-point mode run together during setup or recovery
//
// Reconnection backs off rather than retrying at a fixed rate; the schedule
// itself is decided by domain::NetworkPolicy, which is tested on the host.

namespace stackchan::net {

struct Status {
  domain::NetworkPhase phase = domain::NetworkPhase::unprovisioned;
  bool access_point_up = false;
  // Meaningful only while connected.
  char ip[16] = {};
  // The SSID being broadcast while the access point is up, shown on screen
  // so someone can find it.
  char ap_ssid[24] = {};
  // The access point's passphrase, generated for each boot and shown on the
  // display. The UI provides a serial fallback when no display is available.
  char ap_password[9] = {};
  std::uint8_t consecutive_failures = 0;
};

// Call once at startup. Reads stored credentials and starts connecting when
// they are available. Connection proceeds asynchronously.
[[nodiscard]] bool begin();

// Call periodically. Handles reconnection and starts the setup access point
// after repeated failures. Once started, the access point remains active until
// restart.
void tick(std::uint32_t now_ms);

[[nodiscard]] const Status& status();

// Store credentials, called from the setup pages. The next tick starts
// connecting with them.
[[nodiscard]] bool save_credentials(std::string_view ssid, std::string_view password);

// Whether credentials are stored. The values themselves are not returned.
[[nodiscard]] bool has_credentials();

// The list of nearby networks, for the setup page.
//
// Scanning happens just before the access point goes up because a scan leaves
// its channel. The captured list is reused during setup; an SSID can also be
// entered directly.
struct ScanEntry {
  char ssid[33];
  std::int8_t rssi;
};
[[nodiscard]] std::size_t scan_results(const ScanEntry** out);

}  // namespace stackchan::net
