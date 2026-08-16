#include <unity.h>

#include "stackchan/domain/network_policy.hpp"

using stackchan::domain::NetworkPhase;
using stackchan::domain::NetworkPolicy;

namespace {

// Round numbers for legibility: start at one second, cap at eight, raise an
// access point after three failures.
NetworkPolicy make_policy() { return NetworkPolicy{1000, 8000, 3}; }

void test_starts_unprovisioned() {
  const NetworkPolicy policy = make_policy();
  TEST_ASSERT_EQUAL(NetworkPhase::unprovisioned, policy.phase());
  TEST_ASSERT_FALSE(policy.should_attempt(0));
}

void test_without_credentials_the_access_point_opens_immediately() {
  // A device that cannot connect is never left with no way to configure it.
  const NetworkPolicy policy = make_policy();
  TEST_ASSERT_TRUE(policy.should_open_access_point());
}

void test_credentials_allow_an_attempt() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  TEST_ASSERT_TRUE(policy.should_attempt(0));
}

void test_while_connecting_no_second_attempt_starts() {
  // No second attempt while one is in flight.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_attempt_started(0);
  TEST_ASSERT_EQUAL(NetworkPhase::connecting, policy.phase());
  TEST_ASSERT_FALSE(policy.should_attempt(0));
  TEST_ASSERT_FALSE(policy.should_attempt(100000));
}

void test_connecting_clears_the_access_point_need() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_attempt_started(0);
  policy.on_connected();
  TEST_ASSERT_EQUAL(NetworkPhase::connected, policy.phase());
  TEST_ASSERT_FALSE(policy.should_open_access_point());
  TEST_ASSERT_FALSE(policy.should_attempt(100000));
}

void test_the_wait_doubles_with_each_failure() {
  // A fixed interval would keep hammering at the same rate while the
  // access point is down, wasting airtime and interfering with everything
  // else on the band.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);

  policy.on_failed(0);
  TEST_ASSERT_EQUAL_UINT32(1000, policy.current_backoff_ms());
  policy.on_failed(1000);
  TEST_ASSERT_EQUAL_UINT32(2000, policy.current_backoff_ms());
  policy.on_failed(3000);
  TEST_ASSERT_EQUAL_UINT32(4000, policy.current_backoff_ms());
  policy.on_failed(7000);
  TEST_ASSERT_EQUAL_UINT32(8000, policy.current_backoff_ms());
}

void test_the_wait_stops_at_the_maximum() {
  // Left unbounded, it would take too long to notice the network return.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  for (int i = 0; i < 30; ++i) {
    policy.on_failed(static_cast<std::uint32_t>(i) * 10000);
  }
  TEST_ASSERT_EQUAL_UINT32(8000, policy.current_backoff_ms());
}

void test_no_attempt_during_the_wait() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(5000);

  TEST_ASSERT_FALSE(policy.should_attempt(5000));
  TEST_ASSERT_FALSE(policy.should_attempt(5999));
  TEST_ASSERT_TRUE(policy.should_attempt(6000));
}

void test_wait_remaining_counts_down() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(5000);

  TEST_ASSERT_EQUAL_UINT32(1000, policy.wait_remaining_ms(5000));
  TEST_ASSERT_EQUAL_UINT32(1, policy.wait_remaining_ms(5999));
  TEST_ASSERT_EQUAL_UINT32(0, policy.wait_remaining_ms(6000));
  TEST_ASSERT_EQUAL_UINT32(0, policy.wait_remaining_ms(99999));
}

void test_connecting_resets_the_wait() {
  // Reset on success, so the next disconnection retries promptly.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);
  policy.on_failed(1000);
  policy.on_failed(3000);
  TEST_ASSERT_EQUAL_UINT32(4000, policy.current_backoff_ms());

  policy.on_connected();
  TEST_ASSERT_EQUAL_UINT32(0, policy.current_backoff_ms());
  TEST_ASSERT_EQUAL_UINT8(0, policy.consecutive_failures());

  policy.on_failed(10000);
  TEST_ASSERT_EQUAL_UINT32(1000, policy.current_backoff_ms());
}

void test_the_access_point_opens_after_enough_failures() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);

  policy.on_failed(0);
  TEST_ASSERT_FALSE(policy.should_open_access_point());
  policy.on_failed(1000);
  TEST_ASSERT_FALSE(policy.should_open_access_point());
  // Raised on the third.
  policy.on_failed(3000);
  TEST_ASSERT_TRUE(policy.should_open_access_point());
}

void test_connecting_closes_the_access_point() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);
  policy.on_failed(1000);
  policy.on_failed(3000);
  TEST_ASSERT_TRUE(policy.should_open_access_point());

  policy.on_connected();
  TEST_ASSERT_FALSE(policy.should_open_access_point());
}

void test_losing_the_credentials_returns_to_unprovisioned() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_attempt_started(0);
  policy.on_connected();

  policy.set_provisioned(false);
  TEST_ASSERT_EQUAL(NetworkPhase::unprovisioned, policy.phase());
  TEST_ASSERT_TRUE(policy.should_open_access_point());
  TEST_ASSERT_FALSE(policy.should_attempt(0));
}

void test_failures_are_ignored_without_credentials() {
  // Counting a failure that was never attempted would distort when the
  // access point appears.
  NetworkPolicy policy = make_policy();
  policy.on_failed(0);
  policy.on_failed(1000);
  TEST_ASSERT_EQUAL_UINT8(0, policy.consecutive_failures());
  TEST_ASSERT_EQUAL(NetworkPhase::unprovisioned, policy.phase());
}

void test_reprovisioning_during_backoff_allows_an_immediate_attempt() {
  // Someone who has just corrected a mistyped password should not wait out
  // the backoff earned by the mistake. New credentials mean a different
  // network, so the count starts again.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);
  policy.on_failed(1000);
  policy.on_failed(3000);
  TEST_ASSERT_EQUAL_UINT32(4000, policy.current_backoff_ms());
  TEST_ASSERT_FALSE(policy.should_attempt(3001));

  policy.set_provisioned(true);
  TEST_ASSERT_TRUE(policy.should_attempt(3001));
  TEST_ASSERT_EQUAL_UINT8(0, policy.consecutive_failures());
}

void test_reprovisioning_during_a_connect_attempt_waits_for_its_outcome() {
  // The radio can only be asked for one connection at a time, so new
  // credentials during an attempt wait for its outcome and then start from
  // the shortest interval.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);
  policy.on_failed(1000);
  policy.on_attempt_started(3000);

  policy.set_provisioned(true);
  TEST_ASSERT_EQUAL(NetworkPhase::connecting, policy.phase());
  TEST_ASSERT_FALSE(policy.should_attempt(3000));

  policy.on_failed(9000);
  TEST_ASSERT_EQUAL_UINT32(1000, policy.current_backoff_ms());
  TEST_ASSERT_TRUE(policy.should_attempt(10000));
}

void test_reprovisioning_while_connected_keeps_the_connection() {
  // A working connection is not dropped; new credentials take effect at
  // the next disconnection.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_attempt_started(0);
  policy.on_connected();

  policy.set_provisioned(true);
  TEST_ASSERT_EQUAL(NetworkPhase::connected, policy.phase());
  TEST_ASSERT_FALSE(policy.should_attempt(100000));
}

void test_a_lost_connection_backs_off_before_retrying() {
  // A disconnection counts as a failure. Without that, the state stays at
  // connecting and no further attempt is ever made.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_attempt_started(0);

  policy.on_failed(5000);
  TEST_ASSERT_EQUAL(NetworkPhase::backoff, policy.phase());
  TEST_ASSERT_FALSE(policy.should_attempt(5999));
  TEST_ASSERT_TRUE(policy.should_attempt(6000));
}

void test_retries_pause_while_a_guest_is_connected() {
  // While someone is connected to the access point, do not take the radio
  // away with credentials already known to fail: reconnecting changes
  // channel and disconnects them mid-form.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);

  policy.set_guest_count(1);
  TEST_ASSERT_FALSE(policy.should_attempt(100000));

  policy.set_guest_count(0);
  TEST_ASSERT_TRUE(policy.should_attempt(100000));
}

void test_fresh_credentials_are_tried_even_with_a_guest() {
  // Except once, immediately after new credentials, because whoever
  // entered them is watching for the result.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  policy.on_failed(0);
  policy.set_guest_count(1);
  TEST_ASSERT_FALSE(policy.should_attempt(100000));

  policy.set_provisioned(true);
  TEST_ASSERT_TRUE(policy.should_attempt(100000));

  // If that one attempt also fails, it goes quiet again while anyone is
  // still connected.
  policy.on_attempt_started(100000);
  policy.on_failed(104000);
  TEST_ASSERT_FALSE(policy.should_attempt(200000));
}

void test_survives_millisecond_counter_wraparound() {
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  const std::uint32_t near_max = 0xFFFFFF00u;
  policy.on_failed(near_max);

  TEST_ASSERT_FALSE(policy.should_attempt(near_max));
  // A thousand milliseconds later is past the wrap.
  TEST_ASSERT_FALSE(policy.should_attempt(0x2E7));
  TEST_ASSERT_TRUE(policy.should_attempt(0x2E8));
}

void test_failure_count_does_not_wrap() {
  // Failures keep accumulating over a long outage. Wrapping the count back
  // to zero would drop the access point.
  NetworkPolicy policy = make_policy();
  policy.set_provisioned(true);
  for (int i = 0; i < 300; ++i) {
    policy.on_failed(static_cast<std::uint32_t>(i) * 10000);
  }
  TEST_ASSERT_EQUAL_UINT8(255, policy.consecutive_failures());
  TEST_ASSERT_TRUE(policy.should_open_access_point());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_unprovisioned);
  RUN_TEST(test_without_credentials_the_access_point_opens_immediately);
  RUN_TEST(test_credentials_allow_an_attempt);
  RUN_TEST(test_while_connecting_no_second_attempt_starts);
  RUN_TEST(test_connecting_clears_the_access_point_need);
  RUN_TEST(test_the_wait_doubles_with_each_failure);
  RUN_TEST(test_the_wait_stops_at_the_maximum);
  RUN_TEST(test_no_attempt_during_the_wait);
  RUN_TEST(test_wait_remaining_counts_down);
  RUN_TEST(test_connecting_resets_the_wait);
  RUN_TEST(test_the_access_point_opens_after_enough_failures);
  RUN_TEST(test_connecting_closes_the_access_point);
  RUN_TEST(test_losing_the_credentials_returns_to_unprovisioned);
  RUN_TEST(test_failures_are_ignored_without_credentials);
  RUN_TEST(test_reprovisioning_during_backoff_allows_an_immediate_attempt);
  RUN_TEST(test_reprovisioning_during_a_connect_attempt_waits_for_its_outcome);
  RUN_TEST(test_reprovisioning_while_connected_keeps_the_connection);
  RUN_TEST(test_a_lost_connection_backs_off_before_retrying);
  RUN_TEST(test_retries_pause_while_a_guest_is_connected);
  RUN_TEST(test_fresh_credentials_are_tried_even_with_a_guest);
  RUN_TEST(test_survives_millisecond_counter_wraparound);
  RUN_TEST(test_failure_count_does_not_wrap);
  return UNITY_END();
}
