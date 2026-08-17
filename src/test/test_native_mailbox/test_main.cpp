#include <unity.h>

#include <string>

#include "stackchan/runtime/mailbox.hpp"

using stackchan::runtime::Mailbox;
using stackchan::runtime::OverflowPolicy;

namespace {

void test_starts_empty() {
  const Mailbox<int, 4> box;
  TEST_ASSERT_TRUE(box.empty());
  TEST_ASSERT_FALSE(box.full());
  TEST_ASSERT_EQUAL_UINT32(0, box.size());
  TEST_ASSERT_EQUAL_UINT32(4, box.capacity());
  TEST_ASSERT_EQUAL_UINT32(0, box.dropped());
}

void test_pop_on_empty_returns_nothing() {
  Mailbox<int, 4> box;
  TEST_ASSERT_FALSE(box.pop().has_value());
}

void test_first_in_first_out() {
  Mailbox<int, 4> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_TRUE(box.push(3));

  TEST_ASSERT_EQUAL_INT(1, box.pop().value());
  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
  TEST_ASSERT_EQUAL_INT(3, box.pop().value());
  TEST_ASSERT_TRUE(box.empty());
}

void test_fills_to_capacity() {
  Mailbox<int, 3> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_TRUE(box.push(3));
  TEST_ASSERT_TRUE(box.full());
  TEST_ASSERT_EQUAL_UINT32(3, box.size());
  TEST_ASSERT_EQUAL_UINT32(0, box.dropped());
}

void test_reject_newest_keeps_what_arrived_first() {
  // The default for commands: a later instruction never flushes an earlier
  // one.
  Mailbox<int, 2> box{OverflowPolicy::reject_newest};
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));

  TEST_ASSERT_FALSE(box.push(3));
  TEST_ASSERT_EQUAL_UINT32(1, box.dropped());
  TEST_ASSERT_EQUAL_UINT32(2, box.size());

  TEST_ASSERT_EQUAL_INT(1, box.pop().value());
  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
}

void test_drop_oldest_keeps_the_latest() {
  // For state notifications, where a fresher value is worth more than an
  // old one.
  Mailbox<int, 2> box{OverflowPolicy::drop_oldest};
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));

  // True, because it was accepted — at the cost of discarding the oldest.
  TEST_ASSERT_TRUE(box.push(3));
  TEST_ASSERT_EQUAL_UINT32(1, box.dropped());
  TEST_ASSERT_EQUAL_UINT32(2, box.size());

  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
  TEST_ASSERT_EQUAL_INT(3, box.pop().value());
}

void test_dropped_counts_every_loss() {
  Mailbox<int, 1> box{OverflowPolicy::reject_newest};
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_FALSE(box.push(2));
  TEST_ASSERT_FALSE(box.push(3));
  TEST_ASSERT_FALSE(box.push(4));
  // How badly it is congested is visible, and counting does not stop after
  // the first loss.
  TEST_ASSERT_EQUAL_UINT32(3, box.dropped());
}

void test_wraps_around_the_ring() {
  // Cycling more values than the capacity does not corrupt the positions.
  Mailbox<int, 3> box;
  for (int i = 0; i < 20; ++i) {
    TEST_ASSERT_TRUE(box.push(i));
    TEST_ASSERT_EQUAL_INT(i, box.pop().value());
  }
  TEST_ASSERT_TRUE(box.empty());
  TEST_ASSERT_EQUAL_UINT32(0, box.dropped());
}

void test_interleaved_push_and_pop_keeps_order() {
  Mailbox<int, 3> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_EQUAL_INT(1, box.pop().value());

  TEST_ASSERT_TRUE(box.push(3));
  TEST_ASSERT_TRUE(box.push(4));
  TEST_ASSERT_TRUE(box.full());

  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
  TEST_ASSERT_EQUAL_INT(3, box.pop().value());
  TEST_ASSERT_EQUAL_INT(4, box.pop().value());
}

void test_room_frees_up_after_popping() {
  Mailbox<int, 2> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_FALSE(box.push(3));

  (void)box.pop();
  // Room reappears. A mailbox that filled once must still be usable.
  TEST_ASSERT_TRUE(box.push(3));
  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
  TEST_ASSERT_EQUAL_INT(3, box.pop().value());
}

void test_high_water_mark_records_the_peak() {
  // The record the capacity is reconsidered against.
  Mailbox<int, 4> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_TRUE(box.push(3));
  TEST_ASSERT_EQUAL_UINT32(3, box.high_water_mark());

  (void)box.pop();
  (void)box.pop();
  // It does not fall as entries are taken; the peak is what matters.
  TEST_ASSERT_EQUAL_UINT32(3, box.high_water_mark());

  TEST_ASSERT_TRUE(box.push(4));
  TEST_ASSERT_TRUE(box.push(5));
  TEST_ASSERT_EQUAL_UINT32(3, box.high_water_mark());
}

void test_clear_empties_the_box() {
  Mailbox<int, 4> box;
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.push(2));
  box.clear();
  TEST_ASSERT_TRUE(box.empty());
  TEST_ASSERT_FALSE(box.pop().has_value());
  // Clearing is not a loss.
  TEST_ASSERT_EQUAL_UINT32(0, box.dropped());
}

void test_carries_types_that_own_memory() {
  // Commands carry strings, which have to survive being moved through.
  Mailbox<std::string, 2> box;
  TEST_ASSERT_TRUE(box.push(std::string{"face.set_expression"}));
  TEST_ASSERT_TRUE(box.push(std::string{"estop.engage"}));

  TEST_ASSERT_EQUAL_STRING("face.set_expression", box.pop().value().c_str());
  TEST_ASSERT_EQUAL_STRING("estop.engage", box.pop().value().c_str());
}

void test_popped_slot_does_not_keep_holding_memory() {
  // Holding on to the contents after they are taken would delay their
  // release and distort what the heap looks like.
  Mailbox<std::string, 2> box;
  TEST_ASSERT_TRUE(box.push(std::string(1000, 'x')));
  const auto taken = box.pop();
  TEST_ASSERT_TRUE(taken.has_value());
  TEST_ASSERT_EQUAL_UINT32(1000, taken.value().size());

  // Reusing a slot does not leave anything of the previous occupant.
  TEST_ASSERT_TRUE(box.push(std::string{"short"}));
  TEST_ASSERT_EQUAL_STRING("short", box.pop().value().c_str());
}

void test_capacity_one_behaves_sanely() {
  // The boundary case: a capacity of one expresses "only the latest".
  Mailbox<int, 1> box{OverflowPolicy::drop_oldest};
  TEST_ASSERT_TRUE(box.push(1));
  TEST_ASSERT_TRUE(box.full());
  TEST_ASSERT_TRUE(box.push(2));
  TEST_ASSERT_EQUAL_UINT32(1, box.size());
  TEST_ASSERT_EQUAL_INT(2, box.pop().value());
  TEST_ASSERT_TRUE(box.empty());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_pop_on_empty_returns_nothing);
  RUN_TEST(test_first_in_first_out);
  RUN_TEST(test_fills_to_capacity);
  RUN_TEST(test_reject_newest_keeps_what_arrived_first);
  RUN_TEST(test_drop_oldest_keeps_the_latest);
  RUN_TEST(test_dropped_counts_every_loss);
  RUN_TEST(test_wraps_around_the_ring);
  RUN_TEST(test_interleaved_push_and_pop_keeps_order);
  RUN_TEST(test_room_frees_up_after_popping);
  RUN_TEST(test_high_water_mark_records_the_peak);
  RUN_TEST(test_clear_empties_the_box);
  RUN_TEST(test_carries_types_that_own_memory);
  RUN_TEST(test_popped_slot_does_not_keep_holding_memory);
  RUN_TEST(test_capacity_one_behaves_sanely);
  return UNITY_END();
}
