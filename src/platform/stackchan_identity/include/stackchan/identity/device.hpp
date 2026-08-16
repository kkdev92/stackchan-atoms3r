#pragma once

#include "stackchan/app/envelope.hpp"

// Collects this unit's identity from the hardware.
//
// It cannot live in core for two reasons: reading the MAC address needs
// ESP-IDF, and boot_id needs the hardware random source.
//
// The core side takes only values, so it stays testable on the host.

namespace stackchan::identity {

// Assemble from the MAC address and randomness. Called once at boot.
//
// The returned strings point at static storage, so the caller need not
// keep them alive.
[[nodiscard]] const app::DeviceIdentity& collect();

// Time since boot. 64-bit, so it does not wrap.
[[nodiscard]] std::uint64_t uptime_ms();

}  // namespace stackchan::identity
