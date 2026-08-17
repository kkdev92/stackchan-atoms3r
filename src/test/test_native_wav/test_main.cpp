#include <unity.h>

#include <array>
#include <cstdint>

#include "stackchan/domain/wav.hpp"

using stackchan::domain::kWavHeaderBytes;
using stackchan::domain::write_wav_header;

namespace {

std::uint16_t u16_at(const std::array<std::uint8_t, kWavHeaderBytes>& h,
                     std::size_t at) {
  return static_cast<std::uint16_t>(h[at] | (h[at + 1] << 8));
}

std::uint32_t u32_at(const std::array<std::uint8_t, kWavHeaderBytes>& h,
                     std::size_t at) {
  return static_cast<std::uint32_t>(h[at]) |
         (static_cast<std::uint32_t>(h[at + 1]) << 8) |
         (static_cast<std::uint32_t>(h[at + 2]) << 16) |
         (static_cast<std::uint32_t>(h[at + 3]) << 24);
}

void expect_tag(const std::array<std::uint8_t, kWavHeaderBytes>& h, std::size_t at,
                const char* tag) {
  for (std::size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(tag[i]), h[at + i]);
  }
}

void test_the_header_declares_16khz_mono_s16le() {
  // 16 kHz, 16-bit, mono PCM behind a 44-byte header.
  std::array<std::uint8_t, kWavHeaderBytes> h{};
  write_wav_header(h, 96000);  // three seconds: 48000 samples, two bytes each

  expect_tag(h, 0, "RIFF");
  expect_tag(h, 8, "WAVE");
  expect_tag(h, 12, "fmt ");
  expect_tag(h, 36, "data");

  TEST_ASSERT_EQUAL_UINT32(16, u32_at(h, 16));      // length of the fmt chunk
  TEST_ASSERT_EQUAL_UINT16(1, u16_at(h, 20));       // PCM
  TEST_ASSERT_EQUAL_UINT16(1, u16_at(h, 22));       // mono
  TEST_ASSERT_EQUAL_UINT32(16000, u32_at(h, 24));   // sample rate
  TEST_ASSERT_EQUAL_UINT32(32000, u32_at(h, 28));   // bytes per second
  TEST_ASSERT_EQUAL_UINT16(2, u16_at(h, 32));       // block alignment
  TEST_ASSERT_EQUAL_UINT16(16, u16_at(h, 34));      // bit depth
}

void test_the_sizes_follow_the_pcm_length() {
  std::array<std::uint8_t, kWavHeaderBytes> h{};
  write_wav_header(h, 96000);
  TEST_ASSERT_EQUAL_UINT32(96000, u32_at(h, 40));       // length of the data
  TEST_ASSERT_EQUAL_UINT32(36 + 96000, u32_at(h, 4));   // the total minus 8

  write_wav_header(h, 0);
  TEST_ASSERT_EQUAL_UINT32(0, u32_at(h, 40));
  TEST_ASSERT_EQUAL_UINT32(36, u32_at(h, 4));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_header_declares_16khz_mono_s16le);
  RUN_TEST(test_the_sizes_follow_the_pcm_length);
  return UNITY_END();
}
