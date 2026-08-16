#pragma once

#include <string_view>

#include "stackchan/domain/ids.hpp"
#include "stackchan/domain/json_writer.hpp"
#include "stackchan/domain/protocol.hpp"

// Building the envelope around a message.
//
// Why it is one place
// -------------------
// So that every response has the same shape. Assemble JSON by hand at each
// call site and the shapes drift apart in ways the other end has to cope
// with.
//
// The order things are built
// --------------------------
// The handler writes its payload first, and the envelope is decided from
// the result: success or error. So the payload exists before the envelope
// does, which is why it is written with its own JsonWriter and spliced in.
//
// Doing it the other way round would mean having already written "ok":true
// when the failure becomes known, with no way to take it back.

namespace stackchan::app {

// Who this device is. Filled in at startup.
//
// device_id derives from the chip's own address and survives restarts.
// boot_id is new on every start. Together they let the other end tell "the
// same device restarted" apart from "a different device".
struct DeviceIdentity {
  std::string_view device_id;
  // Defaults to unset(), meaning randomness was not available yet. BootId
  // has no default constructor, so this has to be stated explicitly.
  domain::BootId boot_id = domain::BootId::unset();
  std::string_view firmware_version;
  std::string_view firmware_idf;

  // Identifies which binary is running: the start of the executable's
  // hash.
  //
  // A build timestamp would be useless here, because reproducible builds
  // are enabled — the same source deliberately produces the same bytes,
  // and no timestamp. A hash is what identity actually means then.
  std::string_view firmware_build;
};

// A success response, wrapping a payload that has already been written.
//
// An empty payload is still written as "payload":{}. Omitting the field
// would force the other end to distinguish absent from empty.
void write_result(domain::JsonWriter& out, std::string_view id,
                  std::string_view payload_json) noexcept;

// A failure response.
//
// Whether it may be retried follows from the code, so no caller decides.
void write_error(domain::JsonWriter& out, std::string_view id, domain::ErrorCode code,
                 std::string_view message = {}) noexcept;

// A notification. No id, and no response expected.
void write_event(domain::JsonWriter& out, std::string_view name,
                 std::string_view payload_json) noexcept;

}  // namespace stackchan::app
