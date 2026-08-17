#include <unity.h>

#include "stackchan/runtime/deadline.hpp"

using stackchan::runtime::Deadline;
using stackchan::runtime::Millis;

namespace {

// A time just short of the wrap, used to exercise behaviour across it.
constexpr Millis kNearMax = 0xFFFFFF00u;

void test_never_does_not_expire() {
  const Deadline d = Deadline::never();
  TEST_ASSERT_FALSE(d.bounded());
  TEST_ASSERT_FALSE(d.expired(0));
  TEST_ASSERT_FALSE(d.expired(UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT32(Deadline::kUnbounded, d.remaining(12345));
}

void test_after_expires_when_the_time_comes() {
  const Deadline d = Deadline::after(1000, 500);
  TEST_ASSERT_TRUE(d.bounded());

  TEST_ASSERT_FALSE(d.expired(1000));
  TEST_ASSERT_FALSE(d.expired(1499));
  // The boundary itself counts as expired: there is nothing left to wait
  // for.
  TEST_ASSERT_TRUE(d.expired(1500));
  TEST_ASSERT_TRUE(d.expired(1501));
}

void test_zero_timeout_is_already_expired() {
  // "Try, but do not wait" is expressible as a deadline.
  const Deadline d = Deadline::after(1000, 0);
  TEST_ASSERT_TRUE(d.bounded());
  TEST_ASSERT_TRUE(d.expired(1000));
  TEST_ASSERT_EQUAL_UINT32(0, d.remaining(1000));
}

void test_remaining_counts_down_and_clamps() {
  const Deadline d = Deadline::after(1000, 500);
  TEST_ASSERT_EQUAL_UINT32(500, d.remaining(1000));
  TEST_ASSERT_EQUAL_UINT32(1, d.remaining(1499));
  TEST_ASSERT_EQUAL_UINT32(0, d.remaining(1500));
  // Still zero afterwards: never negative, and never an enormous number.
  TEST_ASSERT_EQUAL_UINT32(0, d.remaining(9999));
}

void test_survives_millisecond_counter_wraparound() {
  // A deadline that falls on the far side of the wrap.
  const Deadline d = Deadline::after(kNearMax, 0x200);

  TEST_ASSERT_FALSE(d.expired(kNearMax));
  TEST_ASSERT_FALSE(d.expired(0xFFFFFFFFu));
  TEST_ASSERT_FALSE(d.expired(0x0FF));  // just past the wrap, not expired yet
  TEST_ASSERT_TRUE(d.expired(0x100));
  TEST_ASSERT_TRUE(d.expired(0x101));
}

void test_remaining_is_correct_across_wraparound() {
  // The deadline lands past the wrap.
  const Deadline d = Deadline::after(kNearMax, 0x200);

  TEST_ASSERT_EQUAL_UINT32(0x200, d.remaining(kNearMax));
  TEST_ASSERT_EQUAL_UINT32(0x101, d.remaining(0xFFFFFFFFu));  // just before the wrap
  TEST_ASSERT_EQUAL_UINT32(0x100, d.remaining(0));            // just after it
  TEST_ASSERT_EQUAL_UINT32(1, d.remaining(0x0FF));
  TEST_ASSERT_EQUAL_UINT32(0, d.remaining(0x100));
}

void test_earlier_of_picks_the_nearer_one() {
  const Millis now = 1000;
  const Deadline soon = Deadline::after(now, 100);
  const Deadline later = Deadline::after(now, 5000);

  TEST_ASSERT_EQUAL_UINT32(100, soon.earlier_of(later, now).remaining(now));
  // The result does not depend on the order of the arguments.
  TEST_ASSERT_EQUAL_UINT32(100, later.earlier_of(soon, now).remaining(now));
}

void test_earlier_of_never_yields_to_a_bounded_one() {
  const Millis now = 1000;
  const Deadline bounded = Deadline::after(now, 100);
  const Deadline unbounded = Deadline::never();

  // Against no deadline, the bounded one wins. Without that, the
  // propagation would break the moment something below passed one up.
  TEST_ASSERT_TRUE(unbounded.earlier_of(bounded, now).bounded());
  TEST_ASSERT_EQUAL_UINT32(100, unbounded.earlier_of(bounded, now).remaining(now));
  TEST_ASSERT_TRUE(bounded.earlier_of(unbounded, now).bounded());
  TEST_ASSERT_EQUAL_UINT32(100, bounded.earlier_of(unbounded, now).remaining(now));
}

void test_earlier_of_two_unbounded_stays_unbounded() {
  const Deadline d = Deadline::never().earlier_of(Deadline::never(), 1000);
  TEST_ASSERT_FALSE(d.bounded());
}

void test_earlier_of_works_across_wraparound() {
  // One before the wrap and one after: comparing the instants directly
  // would invert the answer.
  const Millis now = kNearMax;
  const Deadline before_wrap = Deadline::after(now, 0x80);   // 0xFFFFFF80
  const Deadline after_wrap = Deadline::after(now, 0x180);   // 0x00000080

  // By absolute value the wrapped one looks earlier. Compared as time
  // remaining, the one before the wrap correctly comes first.
  TEST_ASSERT_EQUAL_UINT32(0x80, before_wrap.earlier_of(after_wrap, now).remaining(now));
  TEST_ASSERT_EQUAL_UINT32(0x80, after_wrap.earlier_of(before_wrap, now).remaining(now));
}

void test_a_deadline_can_be_copied_and_passed_by_value() {
  // Cheap to pass by value: no reference and no ownership.
  const Deadline original = Deadline::after(1000, 500);
  const Deadline copy = original;
  TEST_ASSERT_EQUAL_UINT32(original.remaining(1200), copy.remaining(1200));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_never_does_not_expire);
  RUN_TEST(test_after_expires_when_the_time_comes);
  RUN_TEST(test_zero_timeout_is_already_expired);
  RUN_TEST(test_remaining_counts_down_and_clamps);
  RUN_TEST(test_survives_millisecond_counter_wraparound);
  RUN_TEST(test_remaining_is_correct_across_wraparound);
  RUN_TEST(test_earlier_of_picks_the_nearer_one);
  RUN_TEST(test_earlier_of_never_yields_to_a_bounded_one);
  RUN_TEST(test_earlier_of_two_unbounded_stays_unbounded);
  RUN_TEST(test_earlier_of_works_across_wraparound);
  RUN_TEST(test_a_deadline_can_be_copied_and_passed_by_value);
  return UNITY_END();
}
