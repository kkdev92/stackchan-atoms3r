#include "stackchan/app/request_router.hpp"

#include "stackchan/app/envelope.hpp"
#include "stackchan/app/scan_params.hpp"
#include "stackchan/domain/json_scan.hpp"
#include "stackchan/domain/json_writer.hpp"

namespace stackchan::app {
namespace {

// The last resort, for when building the envelope itself has failed.
constexpr std::string_view kLastResort =
    R"({"v":1,"kind":"result","ok":false,)"
    R"("error":{"code":"internal","retryable":false}})";

[[nodiscard]] bool starts_with_object(std::string_view body) noexcept {
  for (const char c : body) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      continue;
    }
    return c == '{';
  }
  return false;
}

}  // namespace

std::string_view RequestRouter::route(std::string_view presented_token,
                                      std::string_view body) noexcept {
  // Authorise, then read the body, then interpret it. Parsing before
  // authorising would expose the parser to anyone who can reach the port.
  if (!token_.matches(presented_token)) {
    return error({}, domain::ErrorCode::bad_request,
                 "X-StackChan-Token header is missing or wrong");
  }

  if (body.empty() || body.size() > kMaxBodyBytes) {
    return error({}, domain::ErrorCode::bad_request, "body is empty or too large");
  }

  if (!starts_with_object(body)) {
    return error({}, domain::ErrorCode::bad_request, "not json");
  }

  // The id is what a response is matched to a request by. An unreadable one
  // — too long, or carrying an escape the contract forbids — is treated as
  // absent rather than truncated, since a truncated id would eventually
  // match some other request.
  std::string_view id{};
  std::string_view raw_id;
  if (domain::json_find_raw_string(body, "id", raw_id)) {
    const std::size_t written =
        domain::json_unescape(raw_id, id_text_.data(), kMaxIdBytes);
    if (written != SIZE_MAX) {
      id = std::string_view{id_text_.data(), written};
    }
  }

  std::string_view raw_name;
  if (!domain::json_find_raw_string(body, "name", raw_name)) {
    return error(id, domain::ErrorCode::bad_request, "name is missing");
  }
  const std::size_t name_written =
      domain::json_unescape(raw_name, name_text_.data(), name_text_.size() - 1);
  if (name_written == SIZE_MAX || name_written == 0) {
    return error(id, domain::ErrorCode::bad_request, "name is missing");
  }
  const std::string_view name{name_text_.data(), name_written};

  // A command with no payload proceeds with an empty reader: absent and
  // "{}" mean the same thing. A present payload must be an object. Letting a
  // wrong type collapse to "absent" is unsafe for commands where absence has
  // behavior of its own, such as starting a microphone recording.
  std::string_view payload_json{};
  const domain::JsonMemberResult payload_result =
      domain::json_find_object_checked(body, "payload", payload_json);
  if (payload_result == domain::JsonMemberResult::invalid) {
    return error(id, domain::ErrorCode::bad_request, "request body is invalid");
  }
  const ScanParams params{payload_json};

  // Build the payload first, then the envelope around it. The other order
  // cannot be undone.
  domain::JsonWriter payload{payload_buffer_.data(), payload_buffer_.size()};
  const domain::ErrorCode code = registry_.dispatch(name, params, payload);

  if (code != domain::ErrorCode::none) {
    return error(id, code);
  }
  if (!payload.valid()) {
    // It overflowed, or a bracket was left open. Do not splice broken JSON
    // into the envelope.
    return error(id, domain::ErrorCode::internal, "response too large");
  }

  domain::JsonWriter envelope{envelope_buffer_.data(), envelope_buffer_.size()};
  write_result(envelope, id, payload.text());
  if (!envelope.valid()) {
    return error(id, domain::ErrorCode::internal, "envelope too large");
  }
  return envelope.text();
}

std::string_view RequestRouter::error(std::string_view id, domain::ErrorCode code,
                                      std::string_view message) noexcept {
  domain::JsonWriter writer{envelope_buffer_.data(), envelope_buffer_.size()};
  write_error(writer, id, code, message);
  if (!writer.valid()) {
    // Reaching here means the envelope itself could not be built. Return
    // the minimum that is still valid.
    return kLastResort;
  }
  return writer.text();
}

}  // namespace stackchan::app
