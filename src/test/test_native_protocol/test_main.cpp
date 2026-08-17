#include <unity.h>

#include <set>
#include <string>
#include <vector>

#include "stackchan/domain/protocol.hpp"

using stackchan::domain::ErrorCode;
using stackchan::domain::is_retryable;
using stackchan::domain::MessageKind;
using stackchan::domain::parse_error_code;
using stackchan::domain::parse_message_kind;
using stackchan::domain::to_string;

namespace {

// Every code. Add new ones here; this is what the coverage checks use.
const std::vector<ErrorCode> kAllCodes = {
    ErrorCode::none,        ErrorCode::bad_request,  ErrorCode::unknown_command,
    ErrorCode::invalid_argument, ErrorCode::not_found, ErrorCode::unsupported,
    ErrorCode::estop_engaged, ErrorCode::busy,       ErrorCode::unavailable,
    ErrorCode::timeout,     ErrorCode::cancelled,    ErrorCode::internal,
};

void test_every_code_has_a_distinct_name() {
  // Duplicate names would be indistinguishable at the other end.
  std::set<std::string> names;
  for (const ErrorCode code : kAllCodes) {
    const std::string name{to_string(code)};
    TEST_ASSERT_FALSE_MESSAGE(name.empty(), "a code has an empty name");
    TEST_ASSERT_TRUE_MESSAGE(names.insert(name).second,
                             "two codes share a name");
  }
  TEST_ASSERT_EQUAL_UINT32(kAllCodes.size(), names.size());
}

void test_every_code_round_trips_through_its_name() {
  // Emitting and parsing read the same table. Changing only one breaks the
  // protocol.
  for (const ErrorCode code : kAllCodes) {
    ErrorCode parsed = ErrorCode::internal;
    TEST_ASSERT_TRUE(parse_error_code(to_string(code), parsed));
    TEST_ASSERT_EQUAL(code, parsed);
  }
}

void test_an_unknown_name_is_rejected_rather_than_guessed() {
  // Falling back to a default for an unknown name would misrepresent it.
  // False is returned so the caller decides.
  ErrorCode parsed = ErrorCode::busy;
  TEST_ASSERT_FALSE(parse_error_code("no_such_code", parsed));
  // The output is left untouched on failure.
  TEST_ASSERT_EQUAL(ErrorCode::busy, parsed);
  TEST_ASSERT_FALSE(parse_error_code("", parsed));
  TEST_ASSERT_EQUAL(ErrorCode::busy, parsed);
}

void test_names_are_not_case_insensitive() {
  // Accepting either case would leave it ambiguous which to send.
  ErrorCode parsed = ErrorCode::none;
  TEST_ASSERT_FALSE(parse_error_code("BUSY", parsed));
  TEST_ASSERT_FALSE(parse_error_code("Busy", parsed));
}

void test_transient_failures_are_retryable() {
  // These can succeed under different conditions.
  TEST_ASSERT_TRUE(is_retryable(ErrorCode::busy));
  TEST_ASSERT_TRUE(is_retryable(ErrorCode::unavailable));
  TEST_ASSERT_TRUE(is_retryable(ErrorCode::timeout));
}

void test_permanent_failures_are_not_retryable() {
  // These give the same answer however often they are sent.
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::bad_request));
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::unknown_command));
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::invalid_argument));
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::not_found));
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::internal));
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::unsupported));
}

void test_an_emergency_stop_is_never_retryable() {
  // The point: retrying automatically would defeat requiring an explicit
  // release.
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::estop_engaged));
}

void test_a_cancellation_is_not_retryable() {
  // The caller stopped it, so nothing restarts it for them.
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::cancelled));
}

void test_success_is_not_retryable() {
  // Success must never prompt a retry.
  TEST_ASSERT_FALSE(is_retryable(ErrorCode::none));
}

void test_message_kinds_round_trip() {
  const MessageKind kinds[] = {MessageKind::command, MessageKind::result,
                              MessageKind::event};
  for (const MessageKind kind : kinds) {
    MessageKind parsed = MessageKind::command;
    TEST_ASSERT_TRUE(parse_message_kind(to_string(kind), parsed));
    TEST_ASSERT_EQUAL(kind, parsed);
  }
}

void test_message_kind_names_are_what_goes_on_the_wire() {
  // The exact strings on the wire. Changing one breaks the other end, so
  // they are pinned.
  TEST_ASSERT_EQUAL_STRING("command", std::string{to_string(MessageKind::command)}.c_str());
  TEST_ASSERT_EQUAL_STRING("result", std::string{to_string(MessageKind::result)}.c_str());
  TEST_ASSERT_EQUAL_STRING("event", std::string{to_string(MessageKind::event)}.c_str());
}

void test_error_names_are_what_goes_on_the_wire() {
  // Likewise: these are matched against by whatever the device talks to.
  TEST_ASSERT_EQUAL_STRING("estop_engaged",
                           std::string{to_string(ErrorCode::estop_engaged)}.c_str());
  TEST_ASSERT_EQUAL_STRING("unknown_command",
                           std::string{to_string(ErrorCode::unknown_command)}.c_str());
  TEST_ASSERT_EQUAL_STRING("invalid_argument",
                           std::string{to_string(ErrorCode::invalid_argument)}.c_str());
  TEST_ASSERT_EQUAL_STRING("unsupported",
                           std::string{to_string(ErrorCode::unsupported)}.c_str());
}

void test_unknown_message_kind_is_rejected() {
  MessageKind parsed = MessageKind::event;
  TEST_ASSERT_FALSE(parse_message_kind("notification", parsed));
  TEST_ASSERT_EQUAL(MessageKind::event, parsed);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_code_has_a_distinct_name);
  RUN_TEST(test_every_code_round_trips_through_its_name);
  RUN_TEST(test_an_unknown_name_is_rejected_rather_than_guessed);
  RUN_TEST(test_names_are_not_case_insensitive);
  RUN_TEST(test_transient_failures_are_retryable);
  RUN_TEST(test_permanent_failures_are_not_retryable);
  RUN_TEST(test_an_emergency_stop_is_never_retryable);
  RUN_TEST(test_a_cancellation_is_not_retryable);
  RUN_TEST(test_success_is_not_retryable);
  RUN_TEST(test_message_kinds_round_trip);
  RUN_TEST(test_message_kind_names_are_what_goes_on_the_wire);
  RUN_TEST(test_error_names_are_what_goes_on_the_wire);
  RUN_TEST(test_unknown_message_kind_is_rejected);
  return UNITY_END();
}
