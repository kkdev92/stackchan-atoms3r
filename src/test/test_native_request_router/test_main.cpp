// RequestRouter: fixes the whole route — authorise, parse the envelope,
// dispatch, respond.
//
// Each step can fail, and each failure has its own envelope. Without these,
// the only way to see one would be to POST to a running device. Here a body
// goes in as a string and an envelope comes out as a string, entirely on
// the host.

#include <unity.h>

#include <string>
#include <string_view>

#include "stackchan/app/command_registry.hpp"
#include "stackchan/app/conversation_start.hpp"
#include "stackchan/app/request_router.hpp"
#include "stackchan/domain/access_token.hpp"
#include "stackchan/domain/json_writer.hpp"
#include "stackchan/domain/protocol.hpp"

using stackchan::app::CommandRegistry;
using stackchan::app::CommandSpec;
using stackchan::app::RequestRouter;
using stackchan::domain::AccessToken;
using stackchan::domain::ErrorCode;
using stackchan::domain::JsonWriter;

void setUp() {}
void tearDown() {}

namespace {

// A fixed 32-character value for the tests.
constexpr std::string_view kToken = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr std::string_view kWrongToken = "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ";

// A handler that echoes its argument, proving the argument path is wired
// up.
ErrorCode handle_echo(void*, const stackchan::ports::ParamReader& params,
                      JsonWriter& payload) {
  std::string_view note;
  if (!params.read_string("note", note)) {
    return ErrorCode::invalid_argument;
  }
  payload.begin_object();
  payload.member("note", note);
  payload.end_object();
  return ErrorCode::none;
}

ErrorCode handle_busy(void*, const stackchan::ports::ParamReader&, JsonWriter&) {
  return ErrorCode::busy;
}

// A handler that writes more payload than there is room for, to exercise
// the overflow path.
ErrorCode handle_flood(void*, const stackchan::ports::ParamReader&,
                       JsonWriter& payload) {
  payload.begin_object();
  for (int i = 0; i < 200; ++i) {
    payload.member("key", "0123456789012345678901234567890123456789");
  }
  payload.end_object();
  return ErrorCode::none;
}

struct ConversationStartSpy {
  int calls = 0;
  std::string last_text;
};

ErrorCode handle_conversation_start(void* context,
                                    const stackchan::ports::ParamReader& params,
                                    JsonWriter& payload) {
  stackchan::app::ConversationStartInput input{};
  const ErrorCode input_error =
      stackchan::app::read_conversation_start_input(params, input);
  if (input_error != ErrorCode::none) {
    return input_error;
  }

  auto& spy = *static_cast<ConversationStartSpy*>(context);
  ++spy.calls;  // stands in for request_start; invalid input must not reach it
  spy.last_text = std::string{input.text};
  payload.begin_object();
  payload.member("accepted", true);
  payload.member("mode", input.listens() ? "listen" : "text");
  payload.end_object();
  return ErrorCode::none;
}

struct Fixture {
  Fixture() {
    token = AccessToken::from_text(kToken);
    TEST_ASSERT_TRUE(registry.add(CommandSpec{"echo.note"}, &handle_echo));
    TEST_ASSERT_TRUE(registry.add(CommandSpec{"always.busy"}, &handle_busy));
    TEST_ASSERT_TRUE(registry.add(CommandSpec{"always.flood"}, &handle_flood));
    TEST_ASSERT_TRUE(registry.add(CommandSpec{"conversation.start"},
                                  &handle_conversation_start, &conversation));
  }
  CommandRegistry registry;
  AccessToken token = AccessToken::unset();
  ConversationStartSpy conversation;
};

[[nodiscard]] bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string conversation_body(std::string_view payload) {
  std::string body = R"({"id":"start","name":"conversation.start")";
  if (!payload.empty()) {
    body += R"(,"payload":)";
    body.append(payload.data(), payload.size());
  }
  body += '}';
  return body;
}

void expect_conversation_argument_error(std::string_view payload) {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string body = conversation_body(payload);
  const std::string response{router.route(kToken, body)};
  TEST_ASSERT_TRUE(contains(response, R"("code":"invalid_argument")"));
  TEST_ASSERT_EQUAL_INT(0, f.conversation.calls);
}

void expect_conversation_payload_shape_error(std::string_view payload) {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string body = conversation_body(payload);
  const std::string response{router.route(kToken, body)};
  TEST_ASSERT_TRUE(contains(response, R"("code":"bad_request")"));
  TEST_ASSERT_EQUAL_INT(0, f.conversation.calls);
}

void expect_conversation_bad_request(std::string_view body) {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string response{router.route(kToken, body)};
  TEST_ASSERT_TRUE(contains(response, R"("code":"bad_request")"));
  TEST_ASSERT_EQUAL_INT(0, f.conversation.calls);
}

}  // namespace

void test_success_envelope_is_exact() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response =
      router.route(kToken, R"({"id":"7","name":"echo.note","payload":{"note":"hi"}})");
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"result","id":"7","ok":true,"payload":{"note":"hi"}})",
      std::string{response}.c_str());
}

void test_wrong_token_is_rejected_before_parsing() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  // A perfectly formed body is still rejected when the token is wrong.
  const std::string_view response =
      router.route(kWrongToken, R"({"id":"7","name":"echo.note"})");
  TEST_ASSERT_TRUE(contains(response, R"("ok":false)"));
  TEST_ASSERT_TRUE(contains(response, R"("code":"bad_request")"));
  TEST_ASSERT_TRUE(contains(response, "X-StackChan-Token"));
  // The id has not been read yet, so it comes back empty.
  TEST_ASSERT_TRUE(contains(response, R"("id":"")"));
}

void test_missing_token_is_rejected() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response = router.route({}, R"({"name":"echo.note"})");
  TEST_ASSERT_TRUE(contains(response, R"("code":"bad_request")"));
}

void test_unset_token_rejects_everything() {
  // A device that could not generate a token refuses everything rather
  // than staying open.
  Fixture f;
  const AccessToken unset = AccessToken::unset();
  RequestRouter router{f.registry, unset};
  const std::string_view response = router.route({}, R"({"name":"echo.note"})");
  TEST_ASSERT_TRUE(contains(response, R"("ok":false)"));
}

void test_empty_body_is_bad_request() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response = router.route(kToken, {});
  TEST_ASSERT_TRUE(contains(response, "body is empty or too large"));
}

void test_oversized_body_is_bad_request() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string body(RequestRouter::kMaxBodyBytes + 1, 'x');
  const std::string_view response = router.route(kToken, body);
  TEST_ASSERT_TRUE(contains(response, "body is empty or too large"));
}

void test_non_json_body_is_bad_request() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response = router.route(kToken, "hello there");
  TEST_ASSERT_TRUE(contains(response, R"("message":"not json")"));
}

void test_missing_name_is_bad_request_with_id_echoed() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response = router.route(kToken, R"({"id":"q-1"})");
  TEST_ASSERT_TRUE(contains(response, R"("id":"q-1")"));
  TEST_ASSERT_TRUE(contains(response, R"("message":"name is missing")"));
}

void test_unknown_command_maps_to_unknown_command() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response =
      router.route(kToken, R"({"id":"2","name":"no.such"})");
  TEST_ASSERT_TRUE(contains(response, R"("code":"unknown_command")"));
  TEST_ASSERT_TRUE(contains(response, R"("id":"2")"));
}

void test_handler_error_code_travels_with_retryable() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response =
      router.route(kToken, R"({"id":"3","name":"always.busy"})");
  TEST_ASSERT_TRUE(contains(response, R"("code":"busy")"));
  // busy can succeed under different conditions, so it is retryable.
  TEST_ASSERT_TRUE(contains(response, R"("retryable":true)"));
}

void test_handler_argument_error() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  // A payload without the expected field: the handler reports
  // invalid_argument.
  const std::string_view response =
      router.route(kToken, R"({"id":"4","name":"echo.note","payload":{}})");
  TEST_ASSERT_TRUE(contains(response, R"("code":"invalid_argument")"));
}

void test_payload_overflow_is_internal_not_garbage() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response =
      router.route(kToken, R"({"id":"5","name":"always.flood"})");
  // Rather than returning half-written broken JSON, it fails as internal.
  TEST_ASSERT_TRUE(contains(response, R"("code":"internal")"));
  TEST_ASSERT_TRUE(contains(response, "response too large"));
  TEST_ASSERT_TRUE(contains(response, R"("retryable":false)"));
}

void test_missing_payload_behaves_like_empty() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  // No payload gives an empty reader, the field cannot be read, and the
  // result is invalid_argument.
  const std::string_view response =
      router.route(kToken, R"({"id":"6","name":"echo.note"})");
  TEST_ASSERT_TRUE(contains(response, R"("code":"invalid_argument")"));
}

void test_conversation_missing_and_empty_payload_are_listen_mode() {
  Fixture f;
  RequestRouter router{f.registry, f.token};

  const std::string missing{
      router.route(kToken, conversation_body(std::string_view{}))};
  TEST_ASSERT_TRUE(contains(missing, R"("ok":true)"));
  TEST_ASSERT_TRUE(contains(missing, R"("mode":"listen")"));
  TEST_ASSERT_EQUAL_INT(1, f.conversation.calls);

  const std::string empty{router.route(kToken, conversation_body("{}"))};
  TEST_ASSERT_TRUE(contains(empty, R"("ok":true)"));
  TEST_ASSERT_TRUE(contains(empty, R"("mode":"listen")"));
  TEST_ASSERT_EQUAL_INT(2, f.conversation.calls);
}

void test_conversation_valid_utf8_text_is_text_mode() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string response{
      router.route(kToken, conversation_body(R"({"text":"こんにちは"})"))};
  TEST_ASSERT_TRUE(contains(response, R"("ok":true)"));
  TEST_ASSERT_TRUE(contains(response, R"("mode":"text")"));
  TEST_ASSERT_EQUAL_INT(1, f.conversation.calls);
  TEST_ASSERT_EQUAL_STRING("こんにちは", f.conversation.last_text.c_str());

  const std::string with_unknown{router.route(
      kToken, conversation_body(R"({"extra":7,"text":"hello"})"))};
  TEST_ASSERT_TRUE(contains(with_unknown, R"("ok":true)"));
  TEST_ASSERT_TRUE(contains(with_unknown, R"("mode":"text")"));
  TEST_ASSERT_EQUAL_INT(2, f.conversation.calls);
  TEST_ASSERT_EQUAL_STRING("hello", f.conversation.last_text.c_str());
}

void test_conversation_text_length_boundary() {
  Fixture f;
  RequestRouter router{f.registry, f.token};

  std::string accepted_payload = R"({"text":")";
  accepted_payload.append(stackchan::app::kMaxConversationStartTextBytes, 'a');
  accepted_payload += R"("})";
  const std::string accepted{
      router.route(kToken, conversation_body(accepted_payload))};
  TEST_ASSERT_TRUE(contains(accepted, R"("ok":true)"));
  TEST_ASSERT_EQUAL_INT(1, f.conversation.calls);
  TEST_ASSERT_EQUAL_UINT32(stackchan::app::kMaxConversationStartTextBytes,
                           f.conversation.last_text.size());

  std::string rejected_payload = R"({"text":")";
  rejected_payload.append(stackchan::app::kMaxConversationStartTextBytes + 1,
                          'b');
  rejected_payload += R"("})";
  const std::string rejected{
      router.route(kToken, conversation_body(rejected_payload))};
  TEST_ASSERT_TRUE(contains(rejected, R"("code":"invalid_argument")"));
  TEST_ASSERT_EQUAL_INT(1, f.conversation.calls);
}

void test_conversation_invalid_text_never_reaches_start() {
  const std::string_view invalid_payloads[] = {
      R"({"text":""})",  R"({"text":123})", R"({"text":null})",
      R"({"text":true})", R"({"text":{}})",  R"({"text":[]})",
      R"({"text":"\u3042"})", R"({"typo":"hello"})",
  };
  for (const std::string_view payload : invalid_payloads) {
    expect_conversation_argument_error(payload);
  }

  std::string arena_exhaustion = R"({"text":")";
  arena_exhaustion.append(513, 'x');
  arena_exhaustion += R"("})";
  expect_conversation_argument_error(arena_exhaustion);
}

void test_non_object_payload_never_reaches_conversation_handler() {
  const std::string_view invalid_payloads[] = {
      R"("hello")", "123", "null", "true", "[]",
  };
  for (const std::string_view payload : invalid_payloads) {
    expect_conversation_payload_shape_error(payload);
  }
}

void test_malformed_or_duplicate_payload_never_reaches_conversation_handler() {
  const std::string_view invalid_bodies[] = {
      R"({"name":"conversation.start","broken":,"payload":{"text":123}})",
      R"({"name":"conversation.start","broken":)",
      R"({"name":"conversation.start","payload":{}} trailing)",
      R"({"name":"conversation.start","payload":{},"payload":{}})",
      R"({"name":"conversation.start","ignored":{]})",
      R"({"name":"conversation.start","ignored":[})",
      R"({"name":"conversation.start","ignored":01})",
      R"({"name":"conversation.start","ignored":"\q"})",
      R"({"name":"conversation.start","payload":{},"after":{"x":]}})",
      R"({"name":"conversation.start","name":"device.state"})",
      R"({"name":"conversation.start","payload":{"text":"hello","text":123}})",
  };
  for (const std::string_view body : invalid_bodies) {
    expect_conversation_bad_request(body);
  }
}

void test_escaped_id_is_unescaped_in_the_envelope() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view response = router.route(
      kToken, R"({"id":"a\tb","name":"echo.note","payload":{"note":"x"}})");
  // Writing it back into the envelope re-escapes it, so a round trip
  // produces the same text.
  TEST_ASSERT_TRUE(contains(response, R"("id":"a\tb")"));
}

void test_overlong_id_is_treated_as_absent() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  std::string body = R"({"id":")";
  body.append(80, 'i');  // longer than kMaxIdBytes
  body += R"(","name":"echo.note","payload":{"note":"x"}})";
  const std::string_view response = router.route(kToken, body);
  // Better returned as absent than as a truncated id that would confuse
  // whoever is matching responses to requests.
  TEST_ASSERT_TRUE(contains(response, R"("id":"")"));
  TEST_ASSERT_TRUE(contains(response, R"("ok":true)"));
}

void test_response_survives_until_next_route() {
  Fixture f;
  RequestRouter router{f.registry, f.token};
  const std::string_view first =
      router.route(kToken, R"({"id":"a","name":"echo.note","payload":{"note":"1"}})");
  const std::string copy{first};
  const std::string_view second =
      router.route(kToken, R"({"id":"b","name":"echo.note","payload":{"note":"2"}})");
  // A second call overwrites the first call's view, as documented. Anyone
  // who took a copy is unaffected.
  TEST_ASSERT_TRUE(contains(second, R"("id":"b")"));
  TEST_ASSERT_TRUE(contains(copy, R"("id":"a")"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_success_envelope_is_exact);
  RUN_TEST(test_wrong_token_is_rejected_before_parsing);
  RUN_TEST(test_missing_token_is_rejected);
  RUN_TEST(test_unset_token_rejects_everything);
  RUN_TEST(test_empty_body_is_bad_request);
  RUN_TEST(test_oversized_body_is_bad_request);
  RUN_TEST(test_non_json_body_is_bad_request);
  RUN_TEST(test_missing_name_is_bad_request_with_id_echoed);
  RUN_TEST(test_unknown_command_maps_to_unknown_command);
  RUN_TEST(test_handler_error_code_travels_with_retryable);
  RUN_TEST(test_handler_argument_error);
  RUN_TEST(test_payload_overflow_is_internal_not_garbage);
  RUN_TEST(test_missing_payload_behaves_like_empty);
  RUN_TEST(test_conversation_missing_and_empty_payload_are_listen_mode);
  RUN_TEST(test_conversation_valid_utf8_text_is_text_mode);
  RUN_TEST(test_conversation_text_length_boundary);
  RUN_TEST(test_conversation_invalid_text_never_reaches_start);
  RUN_TEST(test_non_object_payload_never_reaches_conversation_handler);
  RUN_TEST(test_malformed_or_duplicate_payload_never_reaches_conversation_handler);
  RUN_TEST(test_escaped_id_is_unescaped_in_the_envelope);
  RUN_TEST(test_overlong_id_is_treated_as_absent);
  RUN_TEST(test_response_survives_until_next_route);
  return UNITY_END();
}
