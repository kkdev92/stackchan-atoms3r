#include <unity.h>

#include "stackchan/domain/button.hpp"

using stackchan::domain::ButtonDebouncer;
using stackchan::domain::ButtonEdge;

namespace {

constexpr std::uint32_t kStable = 20;

// ------------------------------------------------------- settling basics

void test_first_call_reports_no_edge() {
  // A button already held at startup was not just pressed.
  ButtonDebouncer b{kStable};
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 0));
  TEST_ASSERT_TRUE(b.is_pressed());
}

void test_first_call_adopts_released_state() {
  ButtonDebouncer b{kStable};
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(false, 0));
  TEST_ASSERT_FALSE(b.is_pressed());
}

void test_press_is_reported_after_stable_period() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);

  // Nothing is believed immediately after the reading changes.
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 100));
  TEST_ASSERT_FALSE(b.is_pressed());

  // Nor just short of the settling interval.
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 100 + kStable - 1));
  TEST_ASSERT_FALSE(b.is_pressed());

  // Reaching it settles the state.
  TEST_ASSERT_EQUAL(ButtonEdge::pressed, b.update(true, 100 + kStable));
  TEST_ASSERT_TRUE(b.is_pressed());
}

void test_edge_is_reported_only_once() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  (void)b.update(true, 100);
  TEST_ASSERT_EQUAL(ButtonEdge::pressed, b.update(true, 120));

  // Holding produces no further edges.
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 200));
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 5000));
}

void test_release_is_reported() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  (void)b.update(true, 100);
  (void)b.update(true, 120);

  (void)b.update(false, 200);
  TEST_ASSERT_TRUE(b.is_pressed());  // not settled yet
  TEST_ASSERT_EQUAL(ButtonEdge::released, b.update(false, 220));
  TEST_ASSERT_FALSE(b.is_pressed());
}

// -------------------------------------------------------------- bounce

void test_chatter_shorter_than_stable_period_is_ignored() {
  // The point of the type: nothing is believed while the contact is
  // bouncing.
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);

  // Bouncing every millisecond, so no reading ever holds long enough.
  for (std::uint32_t t = 100; t < 115; ++t) {
    const bool noisy = (t % 2) == 0;
    TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(noisy, t));
  }
  TEST_ASSERT_FALSE(b.is_pressed());

  // Once it settles, it is believed.
  (void)b.update(true, 120);
  TEST_ASSERT_EQUAL(ButtonEdge::pressed, b.update(true, 140));
}

void test_bounce_back_resets_the_timer() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);

  (void)b.update(true, 100);       // the press begins
  (void)b.update(false, 110);      // it bounces back
  (void)b.update(true, 115);       // and again; timing restarts here

  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 115 + kStable - 1));
  TEST_ASSERT_EQUAL(ButtonEdge::pressed, b.update(true, 115 + kStable));
}

// --------------------------------------------------------- how long held

void test_held_ms_counts_from_the_confirmed_press() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  (void)b.update(true, 100);
  (void)b.update(true, 120);  // settles here

  TEST_ASSERT_EQUAL_UINT32(0, b.held_ms(120));
  TEST_ASSERT_EQUAL_UINT32(880, b.held_ms(1000));
}

void test_held_ms_is_zero_while_released() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  TEST_ASSERT_EQUAL_UINT32(0, b.held_ms(5000));
}

void test_held_ms_is_zero_at_the_moment_of_release() {
  // Easy to get wrong. At the release edge the button is already up, so
  // held_ms is correctly zero. The duration of the press that just ended is
  // last_press_ms.
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  (void)b.update(true, 100);
  (void)b.update(true, 120);   // the press settles
  (void)b.update(false, 300);  // the contact opens
  TEST_ASSERT_EQUAL(ButtonEdge::released, b.update(false, 320));
  TEST_ASSERT_EQUAL_UINT32(0, b.held_ms(320));
}

void test_last_press_ms_is_available_when_released() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  (void)b.update(true, 100);   // the contact closes
  (void)b.update(true, 120);   // the press settles
  (void)b.update(false, 300);  // the contact opens
  (void)b.update(false, 320);  // the release settles

  // Measured between the times the contact moved: 300 - 120 = 180.
  // Measuring between the times the readings settled would give 200, adding
  // the debounce interval to every press.
  TEST_ASSERT_EQUAL_UINT32(180, b.last_press_ms());
}

void test_last_press_ms_is_zero_before_any_release() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);
  TEST_ASSERT_EQUAL_UINT32(0, b.last_press_ms());
  (void)b.update(true, 100);
  (void)b.update(true, 120);
  TEST_ASSERT_EQUAL_UINT32(0, b.last_press_ms());  // not released yet
}

void test_last_press_ms_keeps_the_most_recent_press() {
  ButtonDebouncer b{kStable};
  (void)b.update(false, 0);

  (void)b.update(true, 100);
  (void)b.update(true, 120);
  (void)b.update(false, 200);
  (void)b.update(false, 220);
  TEST_ASSERT_EQUAL_UINT32(80, b.last_press_ms());

  (void)b.update(true, 400);
  (void)b.update(true, 420);
  (void)b.update(false, 900);
  (void)b.update(false, 920);
  TEST_ASSERT_EQUAL_UINT32(480, b.last_press_ms());
}

// ------------------------------------------------------ clock wrap-around

void test_survives_millisecond_counter_wraparound() {
  // The millisecond counter wraps after about 49.7 days. As long as the
  // subtraction is unsigned, the elapsed time stays correct across it.
  ButtonDebouncer b{kStable};
  const std::uint32_t near_max = 0xFFFFFFF0u;

  (void)b.update(false, near_max);
  (void)b.update(true, near_max + 5);          // 0xFFFFFFF5, near the wrap
  TEST_ASSERT_EQUAL(ButtonEdge::none, b.update(true, 0x00000004u));  // 15 ms elapsed
  TEST_ASSERT_EQUAL(ButtonEdge::pressed, b.update(true, 0x00000009u));  // 20 ms elapsed
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_call_reports_no_edge);
  RUN_TEST(test_first_call_adopts_released_state);
  RUN_TEST(test_press_is_reported_after_stable_period);
  RUN_TEST(test_edge_is_reported_only_once);
  RUN_TEST(test_release_is_reported);

  RUN_TEST(test_chatter_shorter_than_stable_period_is_ignored);
  RUN_TEST(test_bounce_back_resets_the_timer);

  RUN_TEST(test_held_ms_counts_from_the_confirmed_press);
  RUN_TEST(test_held_ms_is_zero_while_released);
  RUN_TEST(test_held_ms_is_zero_at_the_moment_of_release);
  RUN_TEST(test_last_press_ms_is_available_when_released);
  RUN_TEST(test_last_press_ms_is_zero_before_any_release);
  RUN_TEST(test_last_press_ms_keeps_the_most_recent_press);

  RUN_TEST(test_survives_millisecond_counter_wraparound);
  return UNITY_END();
}
