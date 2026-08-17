#include <unity.h>

#include <array>
#include <cstdint>
#include <string>

#include "stackchan/domain/base64.hpp"

using stackchan::domain::base64_decode;
using stackchan::domain::base64_decoded_size;

namespace {

// An encoder for the tests, written independently of the implementation so
// the two can be compared.
std::string encode(const std::uint8_t* data, std::size_t size) {
  static const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (std::size_t i = 0; i < size; i += 3) {
    const std::uint32_t b0 = data[i];
    const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
    const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
    const std::uint32_t n = (b0 << 16) | (b1 << 8) | b2;
    out += alphabet[(n >> 18) & 63];
    out += alphabet[(n >> 12) & 63];
    out += i + 1 < size ? alphabet[(n >> 6) & 63] : '=';
    out += i + 2 < size ? alphabet[n & 63] : '=';
  }
  return out;
}

void test_the_classic_vectors() {
  // The examples from RFC 4648.
  std::array<std::uint8_t, 16> out{};
  TEST_ASSERT_EQUAL_UINT32(3, base64_decode("TWFu", out.data(), out.size()));
  TEST_ASSERT_EQUAL_MEMORY("Man", out.data(), 3);
  TEST_ASSERT_EQUAL_UINT32(2, base64_decode("TWE=", out.data(), out.size()));
  TEST_ASSERT_EQUAL_MEMORY("Ma", out.data(), 2);
  TEST_ASSERT_EQUAL_UINT32(1, base64_decode("TQ==", out.data(), out.size()));
  TEST_ASSERT_EQUAL_MEMORY("M", out.data(), 1);
}

void test_empty_input_is_zero_bytes() {
  std::array<std::uint8_t, 4> out{};
  TEST_ASSERT_EQUAL_UINT32(0, base64_decode("", out.data(), out.size()));
  TEST_ASSERT_EQUAL_UINT32(0, base64_decoded_size(""));
}

void test_every_byte_value_round_trips() {
  // Data covering every byte value survives a round trip. Audio contains
  // arbitrary bytes.
  std::array<std::uint8_t, 256> original{};
  for (std::size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<std::uint8_t>(i);
  }
  const std::string text = encode(original.data(), original.size());
  TEST_ASSERT_EQUAL_UINT32(original.size(), base64_decoded_size(text));

  std::array<std::uint8_t, 256> decoded{};
  TEST_ASSERT_EQUAL_UINT32(original.size(),
                           base64_decode(text, decoded.data(), decoded.size()));
  TEST_ASSERT_EQUAL_MEMORY(original.data(), decoded.data(), original.size());
}

void test_odd_lengths_round_trip() {
  // Lengths that exercise both one and two padding characters.
  for (std::size_t size = 1; size <= 9; ++size) {
    std::array<std::uint8_t, 9> original{};
    for (std::size_t i = 0; i < size; ++i) {
      original[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    const std::string text = encode(original.data(), size);
    std::array<std::uint8_t, 9> decoded{};
    TEST_ASSERT_EQUAL_UINT32(size, base64_decode(text, decoded.data(), decoded.size()));
    TEST_ASSERT_EQUAL_MEMORY(original.data(), decoded.data(), size);
  }
}

void test_an_invalid_character_fails() {
  std::array<std::uint8_t, 8> out{};
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TW!u", out.data(), out.size()));
  // Whitespace is invalid too; the contract permits none.
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TW Fu", out.data(), out.size()));
}

void test_a_length_that_is_not_a_multiple_of_four_fails() {
  std::array<std::uint8_t, 8> out{};
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TWF", out.data(), out.size()));
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decoded_size("TWF"));
}

void test_misplaced_padding_fails() {
  std::array<std::uint8_t, 8> out{};
  // Another character after the padding.
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TQ=A", out.data(), out.size()));
  // Padding somewhere other than the final group.
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TQ==TWFu", out.data(), out.size()));
  // Padding at the very start.
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("=QAA", out.data(), out.size()));
}

void test_insufficient_capacity_fails_without_writing_garbage() {
  std::array<std::uint8_t, 2> out{};
  out.fill(0x77);
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, base64_decode("TWFu", out.data(), out.size()));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_classic_vectors);
  RUN_TEST(test_empty_input_is_zero_bytes);
  RUN_TEST(test_every_byte_value_round_trips);
  RUN_TEST(test_odd_lengths_round_trip);
  RUN_TEST(test_an_invalid_character_fails);
  RUN_TEST(test_a_length_that_is_not_a_multiple_of_four_fails);
  RUN_TEST(test_misplaced_padding_fails);
  RUN_TEST(test_insufficient_capacity_fails_without_writing_garbage);
  return UNITY_END();
}
