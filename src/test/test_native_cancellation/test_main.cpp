#include <unity.h>

#include "stackchan/runtime/cancellation.hpp"

using stackchan::runtime::CancellationSource;
using stackchan::runtime::CancellationToken;
using stackchan::runtime::CancelReason;

namespace {

void test_starts_not_cancelled() {
  const CancellationSource source;
  TEST_ASSERT_FALSE(source.cancelled());
  TEST_ASSERT_EQUAL(CancelReason::none, source.reason());
  TEST_ASSERT_FALSE(source.token().cancelled());
}

void test_cancel_is_visible_through_the_token() {
  CancellationSource source;
  const CancellationToken token = source.token();

  // A token taken beforehand still observes a later cancellation. Whoever
  // is waiting usually holds the token before the cancellation happens, so
  // without this it would never arrive.
  TEST_ASSERT_FALSE(token.cancelled());
  source.cancel(CancelReason::requested);
  TEST_ASSERT_TRUE(token.cancelled());
  TEST_ASSERT_EQUAL(CancelReason::requested, token.reason());
}

void test_none_token_is_never_cancelled() {
  const CancellationToken token = CancellationToken::none();
  TEST_ASSERT_FALSE(token.cancelled());
  TEST_ASSERT_EQUAL(CancelReason::none, token.reason());
  TEST_ASSERT_FALSE(token.emergency());
}

void test_a_heavier_reason_wins() {
  CancellationSource source;
  source.cancel(CancelReason::requested);
  TEST_ASSERT_EQUAL(CancelReason::requested, source.reason());

  // An emergency stop during an ordinary cancellation wins, because it is
  // recovered from differently.
  source.cancel(CancelReason::emergency_stop);
  TEST_ASSERT_EQUAL(CancelReason::emergency_stop, source.reason());
}

void test_a_lighter_reason_does_not_overwrite() {
  CancellationSource source;
  source.cancel(CancelReason::emergency_stop);

  // And is not replaced by a timeout or an ordinary stop arriving after
  // it; otherwise the explicit release could not be required.
  source.cancel(CancelReason::timeout);
  TEST_ASSERT_EQUAL(CancelReason::emergency_stop, source.reason());
  source.cancel(CancelReason::requested);
  TEST_ASSERT_EQUAL(CancelReason::emergency_stop, source.reason());
  source.cancel(CancelReason::shutdown);
  TEST_ASSERT_EQUAL(CancelReason::emergency_stop, source.reason());
}

void test_cancelling_twice_with_the_same_reason_is_harmless() {
  CancellationSource source;
  source.cancel(CancelReason::requested);
  source.cancel(CancelReason::requested);
  TEST_ASSERT_EQUAL(CancelReason::requested, source.reason());
}

void test_cancel_with_none_does_nothing() {
  // A caller assembling a reason can end up passing none. That must not
  // put anything into a cancelled state.
  CancellationSource source;
  source.cancel(CancelReason::none);
  TEST_ASSERT_FALSE(source.cancelled());
}

void test_reset_clears_an_ordinary_cancellation() {
  CancellationSource source;
  source.cancel(CancelReason::requested);
  source.reset();
  TEST_ASSERT_FALSE(source.cancelled());
  TEST_ASSERT_FALSE(source.token().cancelled());
}

void test_reset_does_not_clear_an_emergency_stop() {
  // The point: with a reset at the end of every conversation, silently
  // releasing an emergency stop would let the next one start.
  CancellationSource source;
  source.cancel(CancelReason::emergency_stop);

  source.reset();
  TEST_ASSERT_TRUE(source.cancelled());
  TEST_ASSERT_TRUE(source.token().emergency());

  // No number of resets releases it.
  source.reset();
  source.reset();
  TEST_ASSERT_TRUE(source.token().emergency());
}

void test_clear_emergency_releases_it() {
  CancellationSource source;
  source.cancel(CancelReason::emergency_stop);
  source.clear_emergency();
  TEST_ASSERT_FALSE(source.cancelled());
}

void test_clear_emergency_does_not_release_a_shutdown() {
  // A release arriving during a shutdown must not resume anything.
  CancellationSource source;
  source.cancel(CancelReason::shutdown);
  source.clear_emergency();
  TEST_ASSERT_TRUE(source.cancelled());
  TEST_ASSERT_EQUAL(CancelReason::shutdown, source.reason());
}

void test_emergency_is_distinguishable_from_other_reasons() {
  CancellationSource source;
  source.cancel(CancelReason::requested);
  TEST_ASSERT_TRUE(source.token().cancelled());
  TEST_ASSERT_FALSE(source.token().emergency());

  source.cancel(CancelReason::emergency_stop);
  TEST_ASSERT_TRUE(source.token().emergency());
}

void test_tokens_can_be_copied_and_all_see_the_cancellation() {
  // Tokens are passed down through several layers, so copying has to work.
  CancellationSource source;
  const CancellationToken a = source.token();
  const CancellationToken b = a;
  const CancellationToken c = source.token();

  source.cancel(CancelReason::emergency_stop);
  TEST_ASSERT_TRUE(a.cancelled());
  TEST_ASSERT_TRUE(b.cancelled());
  TEST_ASSERT_TRUE(c.cancelled());
  TEST_ASSERT_EQUAL(CancelReason::emergency_stop, b.reason());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_not_cancelled);
  RUN_TEST(test_cancel_is_visible_through_the_token);
  RUN_TEST(test_none_token_is_never_cancelled);
  RUN_TEST(test_a_heavier_reason_wins);
  RUN_TEST(test_a_lighter_reason_does_not_overwrite);
  RUN_TEST(test_cancelling_twice_with_the_same_reason_is_harmless);
  RUN_TEST(test_cancel_with_none_does_nothing);
  RUN_TEST(test_reset_clears_an_ordinary_cancellation);
  RUN_TEST(test_reset_does_not_clear_an_emergency_stop);
  RUN_TEST(test_clear_emergency_releases_it);
  RUN_TEST(test_clear_emergency_does_not_release_a_shutdown);
  RUN_TEST(test_emergency_is_distinguishable_from_other_reasons);
  RUN_TEST(test_tokens_can_be_copied_and_all_see_the_cancellation);
  return UNITY_END();
}
