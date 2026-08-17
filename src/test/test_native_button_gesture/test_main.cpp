#include <unity.h>

#include "stackchan/domain/button_gesture.hpp"

using stackchan::domain::ButtonEdge;
using stackchan::domain::ButtonGesture;
using stackchan::domain::Gesture;
using stackchan::domain::GestureKind;

namespace {

// A one-second window and a half-second hold.
ButtonGesture make_gesture() { return ButtonGesture{1000, 500}; }

// Press and release, letting time pass while held, which is what a hold
// needs.
Gesture tap(ButtonGesture& reader, std::uint32_t at_ms,
            std::uint32_t hold_ms = 50) {
  (void)reader.update(ButtonEdge::pressed, at_ms);
  return reader.update(ButtonEdge::released, at_ms + hold_ms);
}

void test_nothing_happens_without_input() {
  ButtonGesture reader = make_gesture();
  for (std::uint32_t t = 0; t < 5000; t += 100) {
    TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, t).kind);
  }
}

void test_one_tap_is_reported_after_the_window_closes() {
  // Nothing is emitted until the window closes; emitting at once would
  // make a double press impossible.
  ButtonGesture reader = make_gesture();
  TEST_ASSERT_EQUAL(GestureKind::none, tap(reader, 0).kind);
  TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, 999).kind);

  const Gesture gesture = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(1, gesture.clicks);
}

void test_two_taps_inside_the_window_count_as_two() {
  ButtonGesture reader = make_gesture();
  TEST_ASSERT_EQUAL(GestureKind::none, tap(reader, 0).kind);
  TEST_ASSERT_EQUAL(GestureKind::none, tap(reader, 300).kind);

  const Gesture gesture = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(2, gesture.clicks);
}

void test_three_taps_inside_the_window_count_as_three() {
  ButtonGesture reader = make_gesture();
  (void)tap(reader, 0);
  (void)tap(reader, 250);
  (void)tap(reader, 500);

  const Gesture gesture = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(3, gesture.clicks);
}

void test_the_window_is_measured_from_the_first_tap() {
  // The window runs from the first press, not the most recent one.
  // Measured from the most recent, a stream of presses would never settle.
  ButtonGesture reader = make_gesture();
  (void)tap(reader, 0);
  (void)tap(reader, 900);

  const Gesture gesture = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(2, gesture.clicks);
}

void test_a_tap_after_the_window_starts_a_new_count() {
  ButtonGesture reader = make_gesture();
  (void)tap(reader, 0);
  const Gesture first = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL_UINT8(1, first.clicks);

  (void)tap(reader, 1500);
  TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, 2000).kind);
  const Gesture second = reader.update(ButtonEdge::none, 2500);
  TEST_ASSERT_EQUAL(GestureKind::clicks, second.kind);
  TEST_ASSERT_EQUAL_UINT8(1, second.clicks);
}

void test_holding_reports_while_still_pressed() {
  // A hold does not wait for the release: stopping the speech should take
  // effect while the button is still down.
  ButtonGesture reader = make_gesture();
  (void)reader.update(ButtonEdge::pressed, 0);
  TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, 499).kind);
  TEST_ASSERT_EQUAL(GestureKind::hold, reader.update(ButtonEdge::none, 500).kind);
}

void test_holding_is_reported_once() {
  ButtonGesture reader = make_gesture();
  (void)reader.update(ButtonEdge::pressed, 0);
  TEST_ASSERT_EQUAL(GestureKind::hold, reader.update(ButtonEdge::none, 500).kind);
  for (std::uint32_t t = 600; t < 3000; t += 100) {
    TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, t).kind);
  }
  (void)reader.update(ButtonEdge::released, 3000);
  TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, 4500).kind);
}

void test_holding_cancels_the_taps_that_came_before_it() {
  // Someone who only wanted to interrupt does not also start a
  // conversation.
  ButtonGesture reader = make_gesture();
  (void)tap(reader, 0);
  (void)reader.update(ButtonEdge::pressed, 200);
  TEST_ASSERT_EQUAL(GestureKind::hold, reader.update(ButtonEdge::none, 700).kind);

  for (std::uint32_t t = 800; t < 4000; t += 100) {
    TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, t).kind);
  }
}

void test_a_tap_after_a_hold_counts_again() {
  // And can speak again immediately after stopping it.
  ButtonGesture reader = make_gesture();
  (void)reader.update(ButtonEdge::pressed, 0);
  TEST_ASSERT_EQUAL(GestureKind::hold, reader.update(ButtonEdge::none, 500).kind);
  (void)reader.update(ButtonEdge::released, 800);

  (void)tap(reader, 1000);
  const Gesture gesture = reader.update(ButtonEdge::none, 2000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(1, gesture.clicks);
}

void test_pending_clicks_can_be_read_before_the_window_closes() {
  ButtonGesture reader = make_gesture();
  TEST_ASSERT_EQUAL_UINT8(0, reader.pending_clicks());
  (void)tap(reader, 0);
  TEST_ASSERT_EQUAL_UINT8(1, reader.pending_clicks());
  (void)tap(reader, 200);
  TEST_ASSERT_EQUAL_UINT8(2, reader.pending_clicks());
  (void)reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL_UINT8(0, reader.pending_clicks());
}

void test_survives_millisecond_counter_wraparound() {
  ButtonGesture reader = make_gesture();
  const std::uint32_t near_max = 0xFFFFFF00u;
  (void)tap(reader, near_max);
  // A thousand milliseconds later is past the wrap.
  TEST_ASSERT_EQUAL(GestureKind::none, reader.update(ButtonEdge::none, 0x2E7).kind);
  const Gesture gesture = reader.update(ButtonEdge::none, 0x2E8);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(1, gesture.clicks);
}

void test_many_taps_are_reported_as_they_came() {
  // Four or more presses are the caller's business. The count is reported
  // as it is; discarding it is a decision, not a way of counting.
  ButtonGesture reader = make_gesture();
  for (std::uint32_t i = 0; i < 5; ++i) {
    (void)tap(reader, i * 100, 40);
  }
  const Gesture gesture = reader.update(ButtonEdge::none, 1000);
  TEST_ASSERT_EQUAL(GestureKind::clicks, gesture.kind);
  TEST_ASSERT_EQUAL_UINT8(5, gesture.clicks);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_happens_without_input);
  RUN_TEST(test_one_tap_is_reported_after_the_window_closes);
  RUN_TEST(test_two_taps_inside_the_window_count_as_two);
  RUN_TEST(test_three_taps_inside_the_window_count_as_three);
  RUN_TEST(test_the_window_is_measured_from_the_first_tap);
  RUN_TEST(test_a_tap_after_the_window_starts_a_new_count);
  RUN_TEST(test_holding_reports_while_still_pressed);
  RUN_TEST(test_holding_is_reported_once);
  RUN_TEST(test_holding_cancels_the_taps_that_came_before_it);
  RUN_TEST(test_a_tap_after_a_hold_counts_again);
  RUN_TEST(test_pending_clicks_can_be_read_before_the_window_closes);
  RUN_TEST(test_survives_millisecond_counter_wraparound);
  RUN_TEST(test_many_taps_are_reported_as_they_came);
  return UNITY_END();
}
