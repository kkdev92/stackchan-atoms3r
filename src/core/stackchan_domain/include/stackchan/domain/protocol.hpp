#pragma once

#include <cstdint>
#include <string_view>

// What the messages exchanged with the gateway mean.
//
// Why HTTP status codes are not used
// ----------------------------------
// Because the transport is expected to change. The plan is to start over
// HTTP and move to a persistent connection, and status codes do not travel
// on one. Named codes keep their meaning wherever they are carried.
//
// A number like 409 or 503 also leaves the receiver guessing whether
// retrying is sensible. Here, retryable says so outright.
//
// Why errors use explicit categories
// ----------------------------------
// The only clue to what had happened was a message string, so the gateway
// branched on its wording — and changing the wording broke the gateway.

namespace stackchan::domain {

// Raised only for breaking changes, never for added fields.
constexpr std::uint8_t kProtocolVersion = 1;

enum class MessageKind : std::uint8_t {
  command,  // gateway to device; a result is expected
  result,   // device to gateway, answering a command, matched by id
  event,    // device to gateway; no answer, and no id
};

enum class ErrorCode : std::uint8_t {
  none = 0,

  // The request itself is wrong. Sending it again changes nothing.
  bad_request,       // the envelope is malformed
  unknown_command,   // no command is registered under that name
  invalid_argument,  // a value out of range, or an unknown enumerator
  not_found,         // the job or blob referred to does not exist

  // Not answerable now, but answerable under different conditions.
  unsupported,    // implemented, but unusable on this unit (no servo, say)
  estop_engaged,  // emergency stop; needs an explicit release
  busy,           // a resource is in use (asked to record while recording)
  unavailable,    // something depended on is unreachable (gateway is down)

  // Ended while in progress.
  timeout,    // a deadline passed
  cancelled,  // stopped from outside

  internal,  // a defect on this side. Retrying will not help
};

// Whether the request may be retried.
//
// Derived from the code, so callers never have to guess. Leave it ambiguous
// and the gateway retries "just in case", recording twice.
[[nodiscard]] bool is_retryable(ErrorCode code) noexcept;

// The name carried on the wire.
//
// Names rather than numbers, which helps both the human reading a log and a
// peer that has not heard of a newly added value: an unknown name is
// obviously an unknown code, whereas an unknown number invites a
// misreading.
[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

[[nodiscard]] std::string_view to_string(MessageKind kind) noexcept;

// Parse a name back. An unknown name does not fall back to a default:
// false is returned so the caller decides, rather than quietly
// misinterpreting it.
[[nodiscard]] bool parse_error_code(std::string_view text, ErrorCode& out) noexcept;
[[nodiscard]] bool parse_message_kind(std::string_view text, MessageKind& out) noexcept;

}  // namespace stackchan::domain
