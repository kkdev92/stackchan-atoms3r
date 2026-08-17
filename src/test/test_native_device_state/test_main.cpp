#include <unity.h>

#include <string>

#include "stackchan/domain/device_state.hpp"

using stackchan::domain::ConversationPhase;
using stackchan::domain::DeviceState;
using stackchan::domain::Expression;
using stackchan::domain::to_string;

namespace {

// A state in which a conversation can start.
DeviceState ready_state() {
  DeviceState state;
  state.network_connected = true;
  state.gateway_configured = true;
  return state;
}

void test_a_fresh_state_is_idle() {
  const DeviceState state;
  TEST_ASSERT_EQUAL(ConversationPhase::idle, state.conversation);
  TEST_ASSERT_FALSE(state.estop);
  TEST_ASSERT_FALSE(state.audio_busy);
  TEST_ASSERT_EQUAL(Expression::neutral, state.expression);
}

void test_a_ready_device_can_start() {
  const DeviceState state = ready_state();
  TEST_ASSERT_TRUE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("", std::string{state.why_cannot_start()}.c_str());
}

void test_without_a_network_it_cannot_start() {
  // Without a network the gateway is unreachable, and discovering that
  // after starting is too late.
  DeviceState state = ready_state();
  state.network_connected = false;
  TEST_ASSERT_FALSE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("not connected to a network",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_without_a_gateway_url_it_cannot_start() {
  // Do not record with nowhere to send it. Recording and then discarding
  // wastes the speaker's time.
  DeviceState state = ready_state();
  state.gateway_configured = false;
  TEST_ASSERT_FALSE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("gateway url is not configured",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_an_emergency_stop_blocks_starting() {
  DeviceState state = ready_state();
  state.estop = true;
  TEST_ASSERT_FALSE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("emergency stop is engaged",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_a_running_conversation_blocks_starting() {
  DeviceState state = ready_state();
  state.conversation = ConversationPhase::thinking;
  TEST_ASSERT_FALSE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("a conversation is already running",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_busy_audio_blocks_starting() {
  DeviceState state = ready_state();
  state.audio_busy = true;
  TEST_ASSERT_FALSE(state.can_start_conversation());
  TEST_ASSERT_EQUAL_STRING("audio is in use",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_the_emergency_stop_is_reported_before_other_reasons() {
  // Released differently from the others, so reporting the wrong one leaves
  // the caller retrying indefinitely.
  DeviceState state = ready_state();
  state.estop = true;
  state.audio_busy = true;
  state.conversation = ConversationPhase::speaking;
  state.network_connected = false;
  TEST_ASSERT_EQUAL_STRING("emergency stop is engaged",
                           std::string{state.why_cannot_start()}.c_str());
}

void test_every_phase_has_a_name() {
  TEST_ASSERT_EQUAL_STRING("idle", std::string{to_string(ConversationPhase::idle)}.c_str());
  TEST_ASSERT_EQUAL_STRING("listening",
                           std::string{to_string(ConversationPhase::listening)}.c_str());
  TEST_ASSERT_EQUAL_STRING("thinking",
                           std::string{to_string(ConversationPhase::thinking)}.c_str());
  TEST_ASSERT_EQUAL_STRING("speaking",
                           std::string{to_string(ConversationPhase::speaking)}.c_str());
}

void test_the_reason_is_empty_only_when_it_can_start() {
  // Never "cannot start" with an empty reason, which would leave the caller
  // with nothing to say.
  const bool flags[] = {false, true};
  for (const bool estop : flags) {
    for (const bool busy : flags) {
      for (const bool connected : flags) {
        for (const bool gateway : flags) {
          DeviceState state;
          state.estop = estop;
          state.audio_busy = busy;
          state.network_connected = connected;
          state.gateway_configured = gateway;
          const bool can = state.can_start_conversation();
          const bool has_reason = !state.why_cannot_start().empty();
          TEST_ASSERT_EQUAL_MESSAGE(
              can, !has_reason,
              "can start disagrees with whether a reason exists");
        }
      }
    }
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_state_is_idle);
  RUN_TEST(test_a_ready_device_can_start);
  RUN_TEST(test_without_a_network_it_cannot_start);
  RUN_TEST(test_without_a_gateway_url_it_cannot_start);
  RUN_TEST(test_an_emergency_stop_blocks_starting);
  RUN_TEST(test_a_running_conversation_blocks_starting);
  RUN_TEST(test_busy_audio_blocks_starting);
  RUN_TEST(test_the_emergency_stop_is_reported_before_other_reasons);
  RUN_TEST(test_every_phase_has_a_name);
  RUN_TEST(test_the_reason_is_empty_only_when_it_can_start);
  return UNITY_END();
}
