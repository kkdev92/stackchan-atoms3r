// Fixes the shape of the health payload.
//
// It has been broken before: the nested memory object disappeared along
// with two of its fields, and every host test still passed, because nothing
// checked the shape. These close that hole.

#include <unity.h>

#include <array>
#include <string>
#include <string_view>

#include "stackchan/app/health_payload.hpp"

using stackchan::app::to_string;
using stackchan::app::write_health_payload;
using stackchan::domain::HealthLevel;
using stackchan::domain::JsonWriter;
using stackchan::domain::MemorySnapshot;

void setUp() {}
void tearDown() {}

namespace {

std::string render(std::uint64_t uptime, const MemorySnapshot& snapshot,
                   std::uint64_t psram, HealthLevel level) {
  static std::array<char, 512> buffer{};
  JsonWriter writer{buffer.data(), buffer.size()};
  write_health_payload(writer, uptime, snapshot, psram, level);
  TEST_ASSERT_TRUE(writer.valid());
  return std::string{writer.text()};
}

}  // namespace

// Names each field explicitly, so losing any one of them fails here.
void test_payload_shape_is_exact() {
  const MemorySnapshot snapshot{200000, 100000};
  const std::string json = render(1234, snapshot, 8386196, HealthLevel::healthy);
  TEST_ASSERT_EQUAL_STRING(
      R"({"uptime_ms":1234,"level":"healthy","memory":{"internal_free":200000,)"
      R"("largest_internal_block":100000,"psram_free":8386196}})",
      json.c_str());
}

void test_memory_is_nested_not_flattened() {
  // Not the flattened shape that the regression produced.
  const MemorySnapshot snapshot{1, 2};
  const std::string json = render(0, snapshot, 3, HealthLevel::healthy);
  TEST_ASSERT_TRUE(json.find(R"("memory":{)") != std::string::npos);
}

void test_fragmentation_fields_are_present() {
  // A total of free memory hides fragmentation, so both of these are
  // needed.
  const MemorySnapshot snapshot{200000, 4096};
  const std::string json = render(0, snapshot, 0, HealthLevel::degraded);
  TEST_ASSERT_TRUE(json.find(R"("largest_internal_block":4096)") != std::string::npos);
  TEST_ASSERT_TRUE(json.find(R"("psram_free":0)") != std::string::npos);
}

void test_every_level_has_a_name() {
  TEST_ASSERT_EQUAL_STRING("healthy", std::string{to_string(HealthLevel::healthy)}.c_str());
  TEST_ASSERT_EQUAL_STRING("degraded",
                           std::string{to_string(HealthLevel::degraded)}.c_str());
  TEST_ASSERT_EQUAL_STRING("critical",
                           std::string{to_string(HealthLevel::critical)}.c_str());
}

void test_level_travels_into_the_payload() {
  const MemorySnapshot snapshot{0, 0};
  TEST_ASSERT_TRUE(render(0, snapshot, 0, HealthLevel::critical)
                       .find(R"("level":"critical")") != std::string::npos);
  TEST_ASSERT_TRUE(render(0, snapshot, 0, HealthLevel::degraded)
                       .find(R"("level":"degraded")") != std::string::npos);
}

void test_large_values_do_not_lose_precision() {
  // Reported as 64-bit: the uptime is not allowed to wrap.
  const MemorySnapshot snapshot{0, 0};
  const std::string json =
      render(4294967296ULL, snapshot, 8388608, HealthLevel::healthy);
  TEST_ASSERT_TRUE(json.find(R"("uptime_ms":4294967296)") != std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_payload_shape_is_exact);
  RUN_TEST(test_memory_is_nested_not_flattened);
  RUN_TEST(test_fragmentation_fields_are_present);
  RUN_TEST(test_every_level_has_a_name);
  RUN_TEST(test_level_travels_into_the_payload);
  RUN_TEST(test_large_values_do_not_lose_precision);
  return UNITY_END();
}
