// PlaybackStager: fixes the rule that only whole sentences are played.
//
// Two properties are pinned here. However slowly the audio is supplied,
// what reaches the speaker is always a complete sentence — which is what
// keeps playback from breaking up. And the expression changes with the
// sound rather than a sentence ahead of it.

#include <unity.h>

#include <array>

#include "stackchan/app/playback_stager.hpp"

using stackchan::app::PlaybackStager;
using stackchan::domain::Expression;
using stackchan::ports::Sample;

void setUp() {}
void tearDown() {}

namespace {

constexpr std::uint32_t kSinkDepthMs = 240;  // what the audio hardware holds

struct Fixture {
  std::array<Sample, 64> stage{};
  PlaybackStager stager{stage.data(), stage.size(), kSinkDepthMs};

  // Offer n samples of a given value and confirm all of them were taken.
  void feed(std::size_t n, Sample value) {
    std::array<Sample, 64> chunk{};
    for (std::size_t i = 0; i < n; ++i) {
      chunk[i] = value;
    }
    TEST_ASSERT_EQUAL_UINT32(n, stager.accept(chunk.data(), n));
  }
};

}  // namespace

void test_first_sentence_switches_expression_immediately() {
  Fixture f;
  // Nothing is staged, so the expression changes immediately.
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.begin_sentence(Expression::happy));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::happy),
                        static_cast<int>(f.stager.expression_to_speak()));
}

void test_audio_is_held_not_released() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::happy);
  f.feed(10, 100);
  f.feed(10, 200);
  // Supplied in fragments, it accumulates without being played.
  TEST_ASSERT_EQUAL_UINT32(20, f.stager.staged_count());
}

// Pins the bug where the expression ran one sentence ahead of the audio.
void test_next_sentence_flushes_previous_with_previous_expression() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::happy);
  f.feed(20, 1);

  // The second sentence's marker arrives. The answer is: play the first
  // one's twenty samples, still wearing happy.
  TEST_ASSERT_EQUAL_UINT32(20, f.stager.begin_sentence(Expression::sad));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::happy),
                        static_cast<int>(f.stager.expression_to_speak()));

  // Only once those are played does it become sad.
  f.stager.flushed(1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::sad),
                        static_cast<int>(f.stager.expression_to_speak()));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.staged_count());
}

void test_no_new_audio_is_mixed_before_the_boundary_flush() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::happy);
  f.feed(20, 1);
  (void)f.stager.begin_sentence(Expression::sad);

  // Audio arriving before the boundary is flushed is not accepted; taking
  // it would erase where the previous sentence ended.
  std::array<Sample, 4> chunk{};
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.accept(chunk.data(), chunk.size()));

  f.stager.flushed(0);
  TEST_ASSERT_EQUAL_UINT32(4, f.stager.accept(chunk.data(), chunk.size()));
}

void test_overflow_asks_for_early_flush_then_continues() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::neutral);
  f.feed(60, 1);

  // With 64 staged of a 64-sample buffer, offering 10 takes only 4.
  std::array<Sample, 10> chunk{};
  TEST_ASSERT_EQUAL_UINT32(4, f.stager.accept(chunk.data(), chunk.size()));
  TEST_ASSERT_EQUAL_UINT32(64, f.stager.staged_count());

  // Full. The caller plays what is staged and offers the rest again, which
  // is how an unusually long sentence starts playing before it is
  // complete.
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.accept(chunk.data(), 6));
  f.stager.flushed(0);
  TEST_ASSERT_EQUAL_UINT32(6, f.stager.accept(chunk.data(), 6));
  TEST_ASSERT_EQUAL_UINT32(6, f.stager.staged_count());
}

void test_finish_completed_flushes_the_last_sentence() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::happy);
  f.feed(12, 1);
  // The last sentence has no successor, so finish stands in for the
  // boundary.
  TEST_ASSERT_EQUAL_UINT32(12, f.stager.finish(true));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::happy),
                        static_cast<int>(f.stager.expression_to_speak()));
}

void test_finish_cancelled_drops_the_rest() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::happy);
  f.feed(12, 1);
  // Discarded, because speech resuming after a cancellation is unnerving.
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.finish(false));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.staged_count());
}

void test_remaining_ms_counts_down_from_sink_depth() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::neutral);
  f.feed(8, 1);
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.remaining_ms(500));  // nothing played yet
  f.stager.flushed(1000);
  TEST_ASSERT_EQUAL_UINT32(kSinkDepthMs, f.stager.remaining_ms(1000));
  TEST_ASSERT_EQUAL_UINT32(40, f.stager.remaining_ms(1200));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.remaining_ms(1240));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.remaining_ms(9999));
}

void test_remaining_ms_survives_clock_wraparound() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::neutral);
  f.feed(8, 1);
  // Finishing just before the counter wraps.
  const std::uint32_t near_wrap = 0xFFFFFF60;  // 160 ms before the wrap
  f.stager.flushed(near_wrap);
  // 80 ms later, still before the wrap.
  TEST_ASSERT_EQUAL_UINT32(kSinkDepthMs - 80, f.stager.remaining_ms(near_wrap + 80));
  // And after it: the signed difference still counts correctly.
  TEST_ASSERT_EQUAL_UINT32(kSinkDepthMs - 200,
                           f.stager.remaining_ms(near_wrap + 200));  // past the wrap
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.remaining_ms(near_wrap + 240));
}

void test_staged_samples_point_at_the_copied_audio() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::neutral);
  f.feed(3, 7);
  TEST_ASSERT_EQUAL_INT16(7, f.stager.staged_samples()[0]);
  TEST_ASSERT_EQUAL_INT16(7, f.stager.staged_samples()[2]);
}

void test_reset_returns_to_neutral_idle() {
  Fixture f;
  (void)f.stager.begin_sentence(Expression::angry);
  f.feed(5, 1);
  f.stager.flushed(100);
  f.stager.reset();
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.staged_count());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::neutral),
                        static_cast<int>(f.stager.expression_to_speak()));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.remaining_ms(101));
}

void test_null_or_zero_input_is_rejected() {
  Fixture f;
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.accept(nullptr, 8));
  std::array<Sample, 4> chunk{};
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.accept(chunk.data(), 0));
}

void test_two_boundaries_without_audio_between() {
  // A sentence with a marker but no audio does not ask to be flushed.
  Fixture f;
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.begin_sentence(Expression::happy));
  TEST_ASSERT_EQUAL_UINT32(0, f.stager.begin_sentence(Expression::sad));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Expression::sad),
                        static_cast<int>(f.stager.expression_to_speak()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_sentence_switches_expression_immediately);
  RUN_TEST(test_audio_is_held_not_released);
  RUN_TEST(test_next_sentence_flushes_previous_with_previous_expression);
  RUN_TEST(test_no_new_audio_is_mixed_before_the_boundary_flush);
  RUN_TEST(test_overflow_asks_for_early_flush_then_continues);
  RUN_TEST(test_finish_completed_flushes_the_last_sentence);
  RUN_TEST(test_finish_cancelled_drops_the_rest);
  RUN_TEST(test_remaining_ms_counts_down_from_sink_depth);
  RUN_TEST(test_remaining_ms_survives_clock_wraparound);
  RUN_TEST(test_staged_samples_point_at_the_copied_audio);
  RUN_TEST(test_reset_returns_to_neutral_idle);
  RUN_TEST(test_null_or_zero_input_is_rejected);
  RUN_TEST(test_two_boundaries_without_audio_between);
  return UNITY_END();
}
