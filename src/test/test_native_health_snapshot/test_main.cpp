#include <unity.h>

#include "stackchan/domain/health.hpp"

using stackchan::domain::HealthLevel;
using stackchan::domain::MemorySnapshot;
using stackchan::domain::MemoryThresholds;
using stackchan::domain::classify_memory;

void setUp() {}
void tearDown() {}

void test_healthy_memory_is_healthy() {
  const MemoryThresholds thresholds{100'000, 50'000, 20'000};
  const MemorySnapshot snapshot{150'000, 80'000};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthLevel::healthy),
                        static_cast<int>(classify_memory(snapshot, thresholds)));
}

void test_low_total_internal_memory_is_critical() {
  const MemoryThresholds thresholds{100'000, 50'000, 20'000};
  const MemorySnapshot snapshot{49'999, 40'000};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthLevel::critical),
                        static_cast<int>(classify_memory(snapshot, thresholds)));
}

void test_fragmented_internal_memory_is_critical() {
  const MemoryThresholds thresholds{100'000, 50'000, 20'000};
  const MemorySnapshot snapshot{140'000, 19'999};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(HealthLevel::critical),
                        static_cast<int>(classify_memory(snapshot, thresholds)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_memory_is_healthy);
  RUN_TEST(test_low_total_internal_memory_is_critical);
  RUN_TEST(test_fragmented_internal_memory_is_critical);
  return UNITY_END();
}
