#include <unity.h>

#include <array>
#include <string>

#include "stackchan/app/device_describe.hpp"
#include "stackchan/app/envelope.hpp"

using stackchan::app::CommandRegistry;
using stackchan::app::CommandSpec;
using stackchan::app::DeviceIdentity;
using stackchan::app::ParamSpec;
using stackchan::app::write_device_description;
using stackchan::app::write_error;
using stackchan::app::write_event;
using stackchan::app::write_result;
using stackchan::domain::BootId;
using stackchan::domain::ErrorCode;
using stackchan::domain::JsonWriter;

namespace {

ErrorCode ok_handler(void*, const stackchan::ports::ParamReader&, JsonWriter&) {
  return ErrorCode::none;
}

constexpr std::string_view kExpressions[] = {"neutral", "happy", "sad",
                                            "doubt",   "sleepy", "angry"};

DeviceIdentity test_identity() {
  DeviceIdentity id;
  id.device_id = "atoms3r-14c19fd5b6a0";
  id.boot_id = BootId::from_entropy(0, 0);  // all zeroes, to keep it readable
  id.firmware_version = "0.3.0";
  id.firmware_idf = "v6.0.1";
  id.firmware_build = "1a2b3c4d5e6f7890";
  return id;
}

// ---------------------------------------------------- success responses

void test_a_successful_result_carries_the_payload() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_result(w, "req-1", R"({"applied":true})");

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"result","id":"req-1","ok":true,"payload":{"applied":true}})",
      std::string{w.text()}.c_str());
}

void test_an_empty_payload_becomes_an_empty_object() {
  // The field is never omitted, so the other end need not distinguish
  // absent from empty.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_result(w, "req-2", "");

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"result","id":"req-2","ok":true,"payload":{}})",
      std::string{w.text()}.c_str());
}

void test_a_payload_that_is_an_array_is_carried_as_is() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_result(w, "req-3", R"([1,2,3])");
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_TRUE(std::string{w.text()}.find(R"("payload":[1,2,3])") !=
                   std::string::npos);
}

// ---------------------------------------------------- failure responses

void test_an_error_result_says_whether_to_retry() {
  // The point: the other end is never left inferring intent from a status
  // number.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_error(w, "req-4", ErrorCode::busy);

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"result","id":"req-4","ok":false,)"
      R"("error":{"code":"busy","retryable":true}})",
      std::string{w.text()}.c_str());
}

void test_an_emergency_stop_error_is_marked_not_retryable() {
  // Retrying automatically would defeat the point of requiring an explicit
  // release.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_error(w, "req-5", ErrorCode::estop_engaged);

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_TRUE(std::string{w.text()}.find(R"("retryable":false)") !=
                   std::string::npos);
}

void test_an_error_can_carry_a_message() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_error(w, "req-6", ErrorCode::invalid_argument, "expression must be one of six");

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"result","id":"req-6","ok":false,)"
      R"("error":{"code":"invalid_argument",)"
      R"("message":"expression must be one of six","retryable":false}})",
      std::string{w.text()}.c_str());
}

void test_a_message_is_omitted_when_empty() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_error(w, "req-7", ErrorCode::internal);
  TEST_ASSERT_TRUE(std::string{w.text()}.find("message") == std::string::npos);
}

void test_a_message_with_quotes_is_escaped() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_error(w, "req-8", ErrorCode::bad_request, R"(got "x" instead)");
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_TRUE(std::string{w.text()}.find(R"(got \"x\" instead)") !=
                   std::string::npos);
}

// ------------------------------------------------------- notifications

void test_an_event_has_no_id() {
  // Nothing answers these, so there is nothing to match them to.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_event(w, "button.pressed", R"({"clicks":1})");

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"event","name":"button.pressed","payload":{"clicks":1}})",
      std::string{w.text()}.c_str());
  TEST_ASSERT_TRUE(std::string{w.text()}.find(R"("id")") == std::string::npos);
}

void test_an_event_without_a_payload() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_event(w, "estop.cleared", "");
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"kind":"event","name":"estop.cleared","payload":{}})",
      std::string{w.text()}.c_str());
}

// ------------------------------------------------------------ overflow

void test_a_truncated_envelope_is_not_valid() {
  // A partially written document is never sent as a success.
  std::array<char, 16> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_result(w, "a-fairly-long-request-id", R"({"applied":true})");
  TEST_ASSERT_TRUE(w.overflowed());
  TEST_ASSERT_FALSE(w.valid());
}

// ---------------------------------------------------------------- describe

void test_describe_carries_identity_and_capabilities() {
  // The property that matters: capabilities are generated from what is
  // registered.
  CommandRegistry registry;
  static constexpr ParamSpec kParams[] = {{"expression", kExpressions, 6}};
  CommandSpec face{"face.set_expression"};
  face.params = kParams;
  face.param_count = 1;
  TEST_ASSERT_TRUE(registry.add(face, &ok_handler));
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"device.describe"}, &ok_handler));

  std::array<char, 1024> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_device_description(w, test_identity(), registry, 123456);

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"device_id":"atoms3r-14c19fd5b6a0",)"
      R"("boot_id":"00000000000000000000000000","uptime_ms":123456,)"
      R"("firmware":{"version":"0.3.0","idf":"v6.0.1",)"
      R"("build":"1a2b3c4d5e6f7890"},"protocol":1,)"
      R"("capabilities":[{"name":"face.set_expression","params":{"expression":)"
      R"(["neutral","happy","sad","doubt","sleepy","angry"]}},)"
      R"({"name":"device.describe"}]})",
      std::string{w.text()}.c_str());
}

void test_describe_reflects_a_newly_registered_command() {
  // Confirms registration is the only entry point — a new command appears
  // without describe being touched.
  CommandRegistry registry;
  std::array<char, 1024> before_buffer{};
  JsonWriter before{before_buffer.data(), before_buffer.size()};
  write_device_description(before, test_identity(), registry, 0);
  TEST_ASSERT_TRUE(std::string{before.text()}.find("audio.record") ==
                   std::string::npos);

  CommandSpec record{"audio.record"};
  record.is_async = true;
  TEST_ASSERT_TRUE(registry.add(record, &ok_handler));

  std::array<char, 1024> after_buffer{};
  JsonWriter after{after_buffer.data(), after_buffer.size()};
  write_device_description(after, test_identity(), registry, 0);
  TEST_ASSERT_TRUE(after.valid());
  TEST_ASSERT_TRUE(std::string{after.text()}.find(R"({"name":"audio.record","async":true})") !=
                   std::string::npos);
}

void test_describe_does_not_expose_the_ai_service_endpoints() {
  // Nothing about speech recognition or synthesis engines appears: those
  // belong to whatever the device talks to, not to the device.
  CommandRegistry registry;
  std::array<char, 1024> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  write_device_description(w, test_identity(), registry, 0);

  const std::string text{w.text()};
  TEST_ASSERT_TRUE(text.find("whisper") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("piper") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("llm") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("tts_engine") == std::string::npos);
}

void test_describe_uptime_does_not_wrap_at_the_old_limit() {
  // 64-bit, so it keeps increasing well past the point where a 32-bit
  // millisecond counter would have wrapped.
  CommandRegistry registry;
  std::array<char, 512> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  const std::uint64_t beyond_32bit = 5'000'000'000ull;
  write_device_description(w, test_identity(), registry, beyond_32bit);

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_TRUE(std::string{w.text()}.find(R"("uptime_ms":5000000000)") !=
                   std::string::npos);
}

void test_describe_wrapped_in_a_result_envelope() {
  // The shape actually sent: the payload built first, then wrapped.
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"device.describe"}, &ok_handler));

  std::array<char, 512> payload_buffer{};
  JsonWriter payload{payload_buffer.data(), payload_buffer.size()};
  write_device_description(payload, test_identity(), registry, 42);
  TEST_ASSERT_TRUE(payload.valid());

  std::array<char, 1024> envelope_buffer{};
  JsonWriter envelope{envelope_buffer.data(), envelope_buffer.size()};
  write_result(envelope, "req-9", payload.text());

  TEST_ASSERT_TRUE(envelope.valid());
  const std::string text{envelope.text()};
  TEST_ASSERT_EQUAL_UINT32(0, text.find(R"({"v":1,"kind":"result","id":"req-9","ok":true,"payload":{)"));
  TEST_ASSERT_TRUE(text.find(R"("capabilities":[{"name":"device.describe"}])") !=
                   std::string::npos);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_successful_result_carries_the_payload);
  RUN_TEST(test_an_empty_payload_becomes_an_empty_object);
  RUN_TEST(test_a_payload_that_is_an_array_is_carried_as_is);
  RUN_TEST(test_an_error_result_says_whether_to_retry);
  RUN_TEST(test_an_emergency_stop_error_is_marked_not_retryable);
  RUN_TEST(test_an_error_can_carry_a_message);
  RUN_TEST(test_a_message_is_omitted_when_empty);
  RUN_TEST(test_a_message_with_quotes_is_escaped);
  RUN_TEST(test_an_event_has_no_id);
  RUN_TEST(test_an_event_without_a_payload);
  RUN_TEST(test_a_truncated_envelope_is_not_valid);
  RUN_TEST(test_describe_carries_identity_and_capabilities);
  RUN_TEST(test_describe_reflects_a_newly_registered_command);
  RUN_TEST(test_describe_does_not_expose_the_ai_service_endpoints);
  RUN_TEST(test_describe_uptime_does_not_wrap_at_the_old_limit);
  RUN_TEST(test_describe_wrapped_in_a_result_envelope);
  return UNITY_END();
}
