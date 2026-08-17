#pragma once

#include <cstdint>

#include "stackchan/domain/health.hpp"
#include "stackchan/domain/json_writer.hpp"

// Building the payload for device.health.
//
// Why it is in core
// -----------------
// Reading the numbers needs the hardware, but **the shape they are reported
// in** is part of the contract and has nothing to do with hardware. While
// this lived on the platform side, a change dropped the nested memory
// object and two of its fields, and every host test still passed, because
// nothing fixed the shape.
//
// Here, losing a field fails a test.
//
//   { "uptime_ms": n,
//     "level": "healthy" | "degraded" | "critical",
//     "memory": { "internal_free": n,
//                 "largest_internal_block": n,
//                 "psram_free": n } }
//
// largest_internal_block is always included. A total of free memory hides
// fragmentation, and it is the largest contiguous block that the Wi-Fi
// driver needs — so without it, running out of that is invisible from
// outside.

namespace stackchan::app {

// The caller passes an empty JsonWriter; this writes one object into it.
void write_health_payload(domain::JsonWriter& out, std::uint64_t uptime_ms,
                          const domain::MemorySnapshot& snapshot,
                          std::uint64_t psram_free,
                          domain::HealthLevel level) noexcept;

// The name used on the wire, so the spelling of each level lives in one
// place.
[[nodiscard]] std::string_view to_string(domain::HealthLevel level) noexcept;

}  // namespace stackchan::app
