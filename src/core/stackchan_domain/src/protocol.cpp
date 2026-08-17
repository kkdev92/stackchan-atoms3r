#include "stackchan/domain/protocol.hpp"

#include <array>

namespace stackchan::domain {
namespace {

// Codes and their names, kept together so that emitting and parsing cannot
// drift apart.
struct ErrorName {
  ErrorCode code;
  std::string_view text;
};

constexpr std::array<ErrorName, 12> kErrorNames = {{
    {ErrorCode::none, "none"},
    {ErrorCode::bad_request, "bad_request"},
    {ErrorCode::unknown_command, "unknown_command"},
    {ErrorCode::invalid_argument, "invalid_argument"},
    {ErrorCode::not_found, "not_found"},
    {ErrorCode::unsupported, "unsupported"},
    {ErrorCode::estop_engaged, "estop_engaged"},
    {ErrorCode::busy, "busy"},
    {ErrorCode::unavailable, "unavailable"},
    {ErrorCode::timeout, "timeout"},
    {ErrorCode::cancelled, "cancelled"},
    {ErrorCode::internal, "internal"},
}};

struct KindName {
  MessageKind kind;
  std::string_view text;
};

constexpr std::array<KindName, 3> kKindNames = {{
    {MessageKind::command, "command"},
    {MessageKind::result, "result"},
    {MessageKind::event, "event"},
}};

}  // namespace

bool is_retryable(ErrorCode code) noexcept {
  // Every value is listed rather than defaulted, so that adding a code
  // makes -Wswitch point at this function.
  //
  // Why each of these must not be retried:
  //   none             it succeeded; retrying would repeat the work
  //   bad_request      the request is wrong; the same bytes fail the same way
  //   unknown_command  no such command; sending it again changes nothing
  //   invalid_argument the value is wrong; likewise
  //   not_found        what was referred to does not exist
  //   internal         a defect here; a retry hits the same defect
  //   unsupported      this unit cannot do it, restart or not
  //   estop_engaged    a release is required, and an automatic retry would
  //                    defeat the point of having stopped
  //   cancelled        the caller stopped it; do not restart it for them
  switch (code) {
    // These can succeed later, so retrying after a pause is worthwhile.
    case ErrorCode::busy:
    case ErrorCode::unavailable:
    case ErrorCode::timeout:
      return true;

    case ErrorCode::none:
    case ErrorCode::bad_request:
    case ErrorCode::unknown_command:
    case ErrorCode::invalid_argument:
    case ErrorCode::not_found:
    case ErrorCode::internal:
    case ErrorCode::unsupported:
    case ErrorCode::estop_engaged:
    case ErrorCode::cancelled:
      return false;
  }
  return false;
}

std::string_view to_string(ErrorCode code) noexcept {
  for (const auto& entry : kErrorNames) {
    if (entry.code == code) {
      return entry.text;
    }
  }
  return "internal";
}

std::string_view to_string(MessageKind kind) noexcept {
  for (const auto& entry : kKindNames) {
    if (entry.kind == kind) {
      return entry.text;
    }
  }
  return "command";
}

bool parse_error_code(std::string_view text, ErrorCode& out) noexcept {
  for (const auto& entry : kErrorNames) {
    if (entry.text == text) {
      out = entry.code;
      return true;
    }
  }
  return false;
}

bool parse_message_kind(std::string_view text, MessageKind& out) noexcept {
  for (const auto& entry : kKindNames) {
    if (entry.text == text) {
      out = entry.kind;
      return true;
    }
  }
  return false;
}

}  // namespace stackchan::domain
