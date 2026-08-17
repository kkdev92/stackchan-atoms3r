#pragma once

#include "stackchan/app/command_registry.hpp"
#include "stackchan/app/envelope.hpp"

// device.describe: what this unit is, and what it can do.
//
// This is the first thing the other end calls. The capabilities it returns
// decide which operations get sent afterwards.
//
// Nothing here is hand-written JSON
// ---------------------------------
// The capabilities are generated from what is in the CommandRegistry, and
// the identity from what the device knows about itself. Both are assembled
// rather than stored, so neither can fall out of step with reality.
//
// What is deliberately absent
// ---------------------------
// Nothing about speech recognition or synthesis engines. Those run on the
// machine the device talks to, and the device does not know or care which
// they are.

namespace stackchan::app {

// Write the payload that describe returns.
//
// The caller passes an empty JsonWriter; this writes one object into it.
void write_device_description(domain::JsonWriter& out, const DeviceIdentity& identity,
                             const CommandRegistry& registry,
                             std::uint64_t uptime_ms) noexcept;

}  // namespace stackchan::app
