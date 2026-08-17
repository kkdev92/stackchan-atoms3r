#include "stackchan/app/envelope.hpp"

namespace stackchan::app {
namespace {

// What is written when the payload is empty. The field itself is never
// omitted.
constexpr std::string_view kEmptyPayload = "{}";

void write_header(domain::JsonWriter& out, domain::MessageKind kind) noexcept {
  out.begin_object();
  out.member("v", static_cast<std::uint64_t>(domain::kProtocolVersion));
  out.member("kind", domain::to_string(kind));
}

}  // namespace

void write_result(domain::JsonWriter& out, std::string_view id,
                  std::string_view payload_json) noexcept {
  write_header(out, domain::MessageKind::result);
  out.member("id", id);
  out.member("ok", true);
  out.key("payload");
  out.raw_json(payload_json.empty() ? kEmptyPayload : payload_json);
  out.end_object();
}

void write_error(domain::JsonWriter& out, std::string_view id, domain::ErrorCode code,
                 std::string_view message) noexcept {
  write_header(out, domain::MessageKind::result);
  out.member("id", id);
  out.member("ok", false);
  out.key("error");
  out.begin_object();
  out.member("code", domain::to_string(code));
  if (!message.empty()) {
    out.member("message", message);
  }
  // Whether a retry is worthwhile follows from the code, so the other end
  // never has to guess.
  out.member("retryable", domain::is_retryable(code));
  out.end_object();
  out.end_object();
}

void write_event(domain::JsonWriter& out, std::string_view name,
                 std::string_view payload_json) noexcept {
  write_header(out, domain::MessageKind::event);
  out.member("name", name);
  out.key("payload");
  out.raw_json(payload_json.empty() ? kEmptyPayload : payload_json);
  out.end_object();
}

}  // namespace stackchan::app
