#include <unity.h>

#include <cstring>
#include <utility>
#include <vector>

#include "stackchan/runtime/buffer_pool.hpp"

using stackchan::runtime::BufferPool;

namespace {

using Pool = BufferPool<64, 3>;

void test_starts_with_everything_available() {
  const Pool pool;
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(3, pool.available());
  TEST_ASSERT_EQUAL_UINT32(64, Pool::block_bytes());
  TEST_ASSERT_EQUAL_UINT32(3, Pool::block_count());
  TEST_ASSERT_EQUAL_UINT32(0, pool.exhausted_count());
}

void test_acquire_hands_out_usable_memory() {
  Pool pool;
  auto lease = pool.acquire();
  TEST_ASSERT_TRUE(lease.valid());
  TEST_ASSERT_NOT_NULL(lease.data());
  TEST_ASSERT_EQUAL_UINT32(64, lease.size());
  TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());

  // A block can be written to, which is the whole point.
  std::memset(lease.data(), 0xAB, lease.size());
  TEST_ASSERT_EQUAL_HEX8(0xAB, lease.data()[0]);
  TEST_ASSERT_EQUAL_HEX8(0xAB, lease.data()[63]);
}

void test_blocks_do_not_overlap() {
  // Distinct blocks. Overlapping ones would corrupt the audio.
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  TEST_ASSERT_TRUE(a.valid());
  TEST_ASSERT_TRUE(b.valid());
  TEST_ASSERT_NOT_EQUAL(a.data(), b.data());

  std::memset(a.data(), 0x11, a.size());
  std::memset(b.data(), 0x22, b.size());
  TEST_ASSERT_EQUAL_HEX8(0x11, a.data()[0]);
  TEST_ASSERT_EQUAL_HEX8(0x22, b.data()[0]);
}

void test_returns_automatically_when_the_lease_goes_away() {
  Pool pool;
  {
    auto lease = pool.acquire();
    TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());
  }
  // Destroying the lease returns the block, so forgetting to release
  // cannot leak.
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(3, pool.available());
}

void test_explicit_release_returns_it_early() {
  Pool pool;
  auto lease = pool.acquire();
  lease.release();
  TEST_ASSERT_FALSE(lease.valid());
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
}

void test_releasing_twice_is_harmless() {
  Pool pool;
  auto lease = pool.acquire();
  lease.release();
  lease.release();
  // Releasing twice does not corrupt the count.
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(3, pool.available());
}

void test_exhaustion_yields_an_invalid_lease() {
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  auto c = pool.acquire();
  TEST_ASSERT_EQUAL_UINT32(3, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(0, pool.available());

  // The fourth is refused: no crash, and no false success.
  auto d = pool.acquire();
  TEST_ASSERT_FALSE(d.valid());
  TEST_ASSERT_NULL(d.data());
  TEST_ASSERT_EQUAL_UINT32(1, pool.exhausted_count());
}

void test_exhausted_count_records_every_failure() {
  // Running out is observable, and counting does not stop after the first
  // time.
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  auto c = pool.acquire();
  (void)pool.acquire();
  (void)pool.acquire();
  (void)pool.acquire();
  TEST_ASSERT_EQUAL_UINT32(3, pool.exhausted_count());
}

void test_a_returned_block_can_be_taken_again() {
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  auto c = pool.acquire();

  a.release();
  auto d = pool.acquire();
  // A pool that ran dry must still be usable afterwards.
  TEST_ASSERT_TRUE(d.valid());
  TEST_ASSERT_EQUAL_UINT32(3, pool.in_use());
}

void test_writing_to_an_invalid_lease_is_safe() {
  // When the caller forgets to check valid(), nothing crashes.
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  auto c = pool.acquire();

  auto bad = pool.acquire();
  TEST_ASSERT_NULL(bad.data());
  // nullptr comes back, so the mistake is noticed before anything is
  // written.
  TEST_ASSERT_FALSE(static_cast<bool>(bad));
}

void test_move_transfers_the_lease() {
  // Moving is needed to hand a lease between tasks.
  Pool pool;
  auto original = pool.acquire();
  std::memset(original.data(), 0x5A, original.size());
  const std::uint8_t* address = original.data();

  auto moved = std::move(original);
  TEST_ASSERT_TRUE(moved.valid());
  TEST_ASSERT_EQUAL_PTR(address, moved.data());
  TEST_ASSERT_EQUAL_HEX8(0x5A, moved.data()[0]);

  // The source becomes invalid, so the block is not returned twice.
  TEST_ASSERT_FALSE(original.valid());
  TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());
}

void test_moved_from_lease_does_not_return_the_block() {
  Pool pool;
  {
    auto original = pool.acquire();
    auto moved = std::move(original);
    TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());
    // Both are destroyed here, and only one block is returned.
  }
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(3, pool.available());
}

void test_move_assignment_returns_the_previous_block() {
  Pool pool;
  auto a = pool.acquire();
  auto b = pool.acquire();
  TEST_ASSERT_EQUAL_UINT32(2, pool.in_use());

  // What a held is returned, and b's block is taken over.
  a = std::move(b);
  TEST_ASSERT_TRUE(a.valid());
  TEST_ASSERT_FALSE(b.valid());
  TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());
}

void test_self_move_assignment_keeps_the_block() {
  Pool pool;
  auto a = pool.acquire();
  const std::uint8_t* address = a.data();

  a = std::move(a);
  TEST_ASSERT_TRUE(a.valid());
  TEST_ASSERT_EQUAL_PTR(address, a.data());
  TEST_ASSERT_EQUAL_UINT32(1, pool.in_use());
}

void test_high_water_mark_records_the_peak() {
  Pool pool;
  {
    auto a = pool.acquire();
    auto b = pool.acquire();
    TEST_ASSERT_EQUAL_UINT32(2, pool.high_water_mark());
  }
  // The high-water mark does not fall when blocks are returned; it is what
  // the pool size is reconsidered against.
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(2, pool.high_water_mark());
}

void test_all_blocks_are_reachable_over_many_cycles() {
  // Repeated borrowing and returning does not slowly consume the pool. A
  // miscounted return would drain it over hours of running.
  Pool pool;
  for (int cycle = 0; cycle < 50; ++cycle) {
    std::vector<Pool::Lease> held;
    for (int i = 0; i < 3; ++i) {
      auto lease = pool.acquire();
      TEST_ASSERT_TRUE(lease.valid());
      held.push_back(std::move(lease));
    }
    TEST_ASSERT_EQUAL_UINT32(3, pool.in_use());
  }
  TEST_ASSERT_EQUAL_UINT32(0, pool.in_use());
  TEST_ASSERT_EQUAL_UINT32(0, pool.exhausted_count());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_with_everything_available);
  RUN_TEST(test_acquire_hands_out_usable_memory);
  RUN_TEST(test_blocks_do_not_overlap);
  RUN_TEST(test_returns_automatically_when_the_lease_goes_away);
  RUN_TEST(test_explicit_release_returns_it_early);
  RUN_TEST(test_releasing_twice_is_harmless);
  RUN_TEST(test_exhaustion_yields_an_invalid_lease);
  RUN_TEST(test_exhausted_count_records_every_failure);
  RUN_TEST(test_a_returned_block_can_be_taken_again);
  RUN_TEST(test_writing_to_an_invalid_lease_is_safe);
  RUN_TEST(test_move_transfers_the_lease);
  RUN_TEST(test_moved_from_lease_does_not_return_the_block);
  RUN_TEST(test_move_assignment_returns_the_previous_block);
  RUN_TEST(test_self_move_assignment_keeps_the_block);
  RUN_TEST(test_high_water_mark_records_the_peak);
  RUN_TEST(test_all_blocks_are_reachable_over_many_cycles);
  return UNITY_END();
}
