// AudioDirection: verifies the half-duplex state table without audio hardware.

#include <unity.h>

#include "stackchan/domain/audio_direction.hpp"

using stackchan::domain::AudioDirection;
using stackchan::domain::AudioDirectionState;
using stackchan::domain::AudioTransition;

void setUp() {}
void tearDown() {}

namespace {

void expect_state(const AudioDirection& direction, AudioDirectionState expected) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(direction.state()));
}

}  // namespace

void test_starts_idle() {
  const AudioDirection direction;
  expect_state(direction, AudioDirectionState::idle);
  TEST_ASSERT_TRUE(direction.idle());
  TEST_ASSERT_FALSE(direction.capturing());
  TEST_ASSERT_FALSE(direction.playing());
}

void test_capture_from_idle_proceeds() {
  AudioDirection direction;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::proceed),
                        static_cast<int>(direction.begin_capture()));
  expect_state(direction, AudioDirectionState::capturing);
}

void test_playback_from_idle_proceeds() {
  AudioDirection direction;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::proceed),
                        static_cast<int>(direction.begin_playback()));
  expect_state(direction, AudioDirectionState::playing);
}

void test_capture_while_capturing_is_already_active() {
  AudioDirection direction;
  (void)direction.begin_capture();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::already_active),
                        static_cast<int>(direction.begin_capture()));
  expect_state(direction, AudioDirectionState::capturing);
}

void test_playback_while_playing_is_already_active() {
  AudioDirection direction;
  (void)direction.begin_playback();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::already_active),
                        static_cast<int>(direction.begin_playback()));
  expect_state(direction, AudioDirectionState::playing);
}

// These two cases verify that the opposite direction remains active when a
// transition is denied.
void test_capture_while_playing_is_denied_and_playback_survives() {
  AudioDirection direction;
  (void)direction.begin_playback();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::denied),
                        static_cast<int>(direction.begin_capture()));
  // It was refused, and playback is left undisturbed.
  expect_state(direction, AudioDirectionState::playing);
}

void test_playback_while_capturing_is_denied_and_capture_survives() {
  AudioDirection direction;
  (void)direction.begin_capture();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::denied),
                        static_cast<int>(direction.begin_playback()));
  expect_state(direction, AudioDirectionState::capturing);
}

void test_stop_then_switch_is_the_legal_path() {
  // Record, stop, play: the path a conversation takes every turn.
  AudioDirection direction;
  (void)direction.begin_capture();
  direction.end_capture();
  expect_state(direction, AudioDirectionState::idle);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioTransition::proceed),
                        static_cast<int>(direction.begin_playback()));
}

void test_end_capture_does_not_disturb_playback() {
  // Ending a direction that is not running does nothing, so an out-of-order
  // call cannot disturb playback.
  AudioDirection direction;
  (void)direction.begin_playback();
  direction.end_capture();
  expect_state(direction, AudioDirectionState::playing);
}

void test_end_playback_does_not_disturb_capture() {
  AudioDirection direction;
  (void)direction.begin_capture();
  direction.end_playback();
  expect_state(direction, AudioDirectionState::capturing);
}

void test_end_when_idle_is_a_no_op() {
  AudioDirection direction;
  direction.end_capture();
  direction.end_playback();
  expect_state(direction, AudioDirectionState::idle);
}

void test_names_for_logging() {
  TEST_ASSERT_EQUAL_STRING("idle", to_string(AudioDirectionState::idle));
  TEST_ASSERT_EQUAL_STRING("capturing", to_string(AudioDirectionState::capturing));
  TEST_ASSERT_EQUAL_STRING("playing", to_string(AudioDirectionState::playing));
  TEST_ASSERT_EQUAL_STRING("proceed", to_string(AudioTransition::proceed));
  TEST_ASSERT_EQUAL_STRING("already_active", to_string(AudioTransition::already_active));
  TEST_ASSERT_EQUAL_STRING("denied", to_string(AudioTransition::denied));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle);
  RUN_TEST(test_capture_from_idle_proceeds);
  RUN_TEST(test_playback_from_idle_proceeds);
  RUN_TEST(test_capture_while_capturing_is_already_active);
  RUN_TEST(test_playback_while_playing_is_already_active);
  RUN_TEST(test_capture_while_playing_is_denied_and_playback_survives);
  RUN_TEST(test_playback_while_capturing_is_denied_and_capture_survives);
  RUN_TEST(test_stop_then_switch_is_the_legal_path);
  RUN_TEST(test_end_capture_does_not_disturb_playback);
  RUN_TEST(test_end_playback_does_not_disturb_capture);
  RUN_TEST(test_end_when_idle_is_a_no_op);
  RUN_TEST(test_names_for_logging);
  return UNITY_END();
}
