#include <unity.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

#include "stackchan/runtime/receive_exact.hpp"

using stackchan::runtime::receive_exact;
using stackchan::runtime::ReceiveExactResult;

void setUp() {}
void tearDown() {}

void test_accepts_one_complete_chunk() {
  constexpr std::string_view source{"body"};
  std::array<char, source.size()> destination{};
  std::size_t calls = 0;

  const ReceiveExactResult result = receive_exact(
      destination.data(), destination.size(),
      [&](char* out, std::size_t remaining) {
        ++calls;
        std::memcpy(out, source.data(), remaining);
        return static_cast<int>(remaining);
      });

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ReceiveExactResult::complete),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(1, calls);
  TEST_ASSERT_EQUAL_MEMORY(source.data(), destination.data(), source.size());
}

void test_assembles_a_provisioning_body_received_in_chunks() {
  constexpr std::string_view first{R"({"ssid":"Lab",)"};
  constexpr std::string_view second{R"("pass":"secret"})"};
  constexpr std::string_view full{R"({"ssid":"Lab","pass":"secret"})"};
  std::array<char, full.size()> destination{};
  std::array<std::size_t, 2> requested{};
  std::size_t calls = 0;

  const ReceiveExactResult result = receive_exact(
      destination.data(), destination.size(),
      [&](char* out, std::size_t remaining) {
        requested[calls] = remaining;
        const std::string_view chunk = calls++ == 0 ? first : second;
        std::memcpy(out, chunk.data(), chunk.size());
        return static_cast<int>(chunk.size());
      });

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ReceiveExactResult::complete),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(2, calls);
  TEST_ASSERT_EQUAL_UINT32(full.size(), requested[0]);
  TEST_ASSERT_EQUAL_UINT32(second.size(), requested[1]);
  TEST_ASSERT_EQUAL_MEMORY(full.data(), destination.data(), full.size());
}

void test_zero_result_after_a_partial_read_reports_incomplete() {
  std::array<char, 6> destination{};
  std::size_t calls = 0;

  const ReceiveExactResult result = receive_exact(
      destination.data(), destination.size(),
      [&](char* out, std::size_t) {
        ++calls;
        if (calls == 1) {
          std::memcpy(out, "abc", 3);
          return 3;
        }
        return 0;
      });

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ReceiveExactResult::incomplete),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(2, calls);
  TEST_ASSERT_EQUAL_MEMORY("abc", destination.data(), 3);
}

void test_negative_result_after_a_partial_read_reports_error() {
  std::array<char, 6> destination{};
  std::size_t calls = 0;

  const ReceiveExactResult result = receive_exact(
      destination.data(), destination.size(),
      [&](char* out, std::size_t) {
        ++calls;
        if (calls == 1) {
          std::memcpy(out, "abc", 3);
          return 3;
        }
        return -1;
      });

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ReceiveExactResult::error),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(2, calls);
  TEST_ASSERT_EQUAL_MEMORY("abc", destination.data(), 3);
}

void test_rejects_a_receiver_that_reports_more_than_requested() {
  std::array<char, 4> destination{};
  std::size_t calls = 0;

  const ReceiveExactResult result = receive_exact(
      destination.data(), destination.size(),
      [&](char*, std::size_t remaining) {
        ++calls;
        return static_cast<int>(remaining + 1);
      });

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ReceiveExactResult::error),
                        static_cast<int>(result));
  TEST_ASSERT_EQUAL_UINT32(1, calls);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_accepts_one_complete_chunk);
  RUN_TEST(test_assembles_a_provisioning_body_received_in_chunks);
  RUN_TEST(test_zero_result_after_a_partial_read_reports_incomplete);
  RUN_TEST(test_negative_result_after_a_partial_read_reports_error);
  RUN_TEST(test_rejects_a_receiver_that_reports_more_than_requested);
  return UNITY_END();
}
