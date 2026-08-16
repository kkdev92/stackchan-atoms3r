// The segmenter cuts a reply into sentences to be spoken one at a time.
//
// The sample text here is Japanese on purpose, and should stay that way. It
// is what exercises the two things this type exists to get right: sentences
// end in characters that ASCII does not have (。！？), and every character
// is several bytes, so a cut in the wrong place produces a byte sequence
// that is not text at all. English test data would pass without touching
// either problem.
//
// Failure messages are in English, because they are what a contributor sees
// when something breaks.

#include <unity.h>

#include <string>
#include <utility>
#include <vector>

#include "stackchan/domain/speech.hpp"

using stackchan::domain::Expression;
using stackchan::domain::SpeechSegmenter;

namespace {

using Piece = std::pair<Expression, std::string>;

std::vector<Piece> drain(SpeechSegmenter& segmenter) {
  std::vector<Piece> out;
  SpeechSegmenter::Segment segment;
  while (segmenter.next_segment(segment)) {
    out.emplace_back(segment.expression, std::string{segment.text});
  }
  return out;
}

std::vector<Piece> run_whole(std::string_view text, std::size_t max_bytes = 60) {
  SpeechSegmenter segmenter{max_bytes};
  segmenter.feed(text);
  segmenter.flush();
  return drain(segmenter);
}

void assert_pieces(const std::vector<Piece>& expected, const std::vector<Piece>& actual,
                   const char* label) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), actual.size(), label);
  for (std::size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_MESSAGE(expected[i].first, actual[i].first, label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[i].second.c_str(),
                                     actual[i].second.c_str(), label);
  }
}

// ------------------------------------------------- where sentences end

void test_a_japanese_sentence_ends_at_the_terminator() {
  const auto pieces = run_whole("こんにちは。元気です。");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL_STRING("こんにちは。", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("元気です。", pieces[1].second.c_str());
}

void test_fullwidth_exclamation_and_question_terminate() {
  const auto pieces = run_whole("すごい！なぜ？");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL_STRING("すごい！", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("なぜ？", pieces[1].second.c_str());
}

void test_ascii_bang_and_question_terminate() {
  const auto pieces = run_whole("Wow!Really?");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL_STRING("Wow!", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("Really?", pieces[1].second.c_str());
}

void test_a_newline_is_a_boundary_but_not_part_of_the_text() {
  const auto pieces = run_whole("一行目\n二行目");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL_STRING("一行目", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("二行目", pieces[1].second.c_str());
}

void test_a_dot_inside_a_number_does_not_split() {
  // A decimal point is not a full stop: "3.14" stays in one piece.
  const auto pieces = run_whole("円周率は 3.14 です。");
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("円周率は 3.14 です。", pieces[0].second.c_str());
}

void test_a_dot_before_a_space_terminates() {
  const auto pieces = run_whole("Yes. No.");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL_STRING("Yes.", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("No.", pieces[1].second.c_str());
}

void test_a_dot_at_the_end_of_the_stream_terminates() {
  const auto pieces = run_whole("End.");
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("End.", pieces[0].second.c_str());
}

void test_leading_spaces_are_not_kept() {
  const auto pieces = run_whole("One. Two.");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  // The space that separated two sentences does not begin the second one.
  TEST_ASSERT_EQUAL_STRING("Two.", pieces[1].second.c_str());
}

// ------------------------------------------------------------- markers

void test_a_marker_switches_the_expression() {
  const auto pieces = run_whole("[happy]やった。[sad]残念。");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL(Expression::happy, pieces[0].first);
  TEST_ASSERT_EQUAL_STRING("やった。", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL(Expression::sad, pieces[1].first);
  TEST_ASSERT_EQUAL_STRING("残念。", pieces[1].second.c_str());
}

void test_the_default_expression_is_neutral() {
  const auto pieces = run_whole("こんにちは。");
  TEST_ASSERT_EQUAL(Expression::neutral, pieces[0].first);
}

void test_the_expression_carries_across_sentences() {
  const auto pieces = run_whole("[angry]一。二。");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL(Expression::angry, pieces[0].first);
  TEST_ASSERT_EQUAL(Expression::angry, pieces[1].first);
}

void test_a_marker_mid_sentence_forces_a_boundary() {
  // A marker part-way through ends the sentence, so what came before it is
  // emitted under the expression that was in force. One sentence never
  // carries two expressions.
  const auto pieces = run_whole("晴れです[sad]でも明日は雨。");
  TEST_ASSERT_EQUAL_UINT32(2, pieces.size());
  TEST_ASSERT_EQUAL(Expression::neutral, pieces[0].first);
  TEST_ASSERT_EQUAL_STRING("晴れです", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL(Expression::sad, pieces[1].first);
  TEST_ASSERT_EQUAL_STRING("でも明日は雨。", pieces[1].second.c_str());
}

void test_an_unknown_marker_stays_in_the_text() {
  // Ordinary text is left alone, brackets included: "[1, 2, 3]" survives.
  const auto pieces = run_whole("[unknown]配列は[1, 2, 3]です。");
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("[unknown]配列は[1, 2, 3]です。", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL(Expression::neutral, pieces[0].first);
}

void test_an_unclosed_marker_at_the_end_stays_in_the_text() {
  const auto pieces = run_whole("おわり[hap");
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("おわり[hap", pieces[0].second.c_str());
}

void test_uppercase_is_not_a_marker() {
  // Expression names are lower case, so "[Happy]" is text, not a marker.
  const auto pieces = run_whole("[Happy]テスト。");
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("[Happy]テスト。", pieces[0].second.c_str());
}

void test_every_expression_name_works_as_a_marker() {
  for (std::size_t i = 0; i < stackchan::domain::kExpressionCount; ++i) {
    const std::string text =
        "[" + std::string{stackchan::domain::kExpressionNames[i]} + "]文。";
    const auto pieces = run_whole(text);
    TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
    TEST_ASSERT_EQUAL(static_cast<Expression>(i), pieces[0].first);
    TEST_ASSERT_EQUAL_STRING("文。", pieces[0].second.c_str());
  }
}

// -------------------------------------------------------- length limits

void test_a_long_sentence_is_cut_at_the_limit() {
  // With a ten-byte limit and three-byte characters, the cut lands at nine.
  const auto pieces = run_whole("あいうえおかきくけこ", 10);
  TEST_ASSERT_EQUAL_UINT32(4, pieces.size());
  TEST_ASSERT_EQUAL_STRING("あいう", pieces[0].second.c_str());
  TEST_ASSERT_EQUAL_STRING("えおか", pieces[1].second.c_str());
  TEST_ASSERT_EQUAL_STRING("きくけ", pieces[2].second.c_str());
  TEST_ASSERT_EQUAL_STRING("こ", pieces[3].second.c_str());
}

void test_the_cut_never_lands_inside_a_utf8_character() {
  // Whatever the limit, every emitted sentence begins and ends on a
  // complete UTF-8 character.
  for (std::size_t max = 8; max <= 16; ++max) {
    const auto pieces = run_whole("日本語のとても長い文がここに続いています", max);
    for (const auto& piece : pieces) {
      const auto& text = piece.second;
      TEST_ASSERT_FALSE_MESSAGE(text.empty(), "a segment came out empty");
      // A continuation byte first would mean the previous character was cut
      // in half.
      TEST_ASSERT_TRUE_MESSAGE(
          (static_cast<unsigned char>(text.front()) & 0xC0) != 0x80,
          "segment starts on a UTF-8 continuation byte");
      // Find the last lead byte and check its character is complete.
      std::size_t lead = text.size();
      while (lead > 0 && (static_cast<unsigned char>(text[lead - 1]) & 0xC0) == 0x80) {
        --lead;
      }
      const unsigned char lead_byte = static_cast<unsigned char>(text[lead - 1]);
      std::size_t char_len = 1;
      if ((lead_byte & 0xF8) == 0xF0) {
        char_len = 4;
      } else if ((lead_byte & 0xF0) == 0xE0) {
        char_len = 3;
      } else if ((lead_byte & 0xE0) == 0xC0) {
        char_len = 2;
      }
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(
          text.size(), lead - 1 + char_len,
          "segment ends part-way through a UTF-8 character");
    }
  }
}

// ------------------------------------------- the split must not matter

void test_every_split_point_equals_whole() {
  // This is the point of the whole type: wherever the deltas are split, the
  // result is the same. The stream used here contains markers, multi-byte
  // sentence endings, a decimal point and newlines.
  const std::string stream =
      "[happy]こんにちは！円周率は 3.14 です。[sad]雨です\n"
      "[unknown]そのまま。おわり";
  const auto expected = run_whole(stream);
  TEST_ASSERT_EQUAL_UINT32(5, expected.size());

  for (std::size_t split = 1; split < stream.size(); ++split) {
    SpeechSegmenter segmenter{60};
    segmenter.feed(std::string_view{stream}.substr(0, split));
    auto first = drain(segmenter);  // draining part-way through is safe
    segmenter.feed(std::string_view{stream}.substr(split));
    segmenter.flush();
    const auto second = drain(segmenter);

    auto all = first;
    all.insert(all.end(), second.begin(), second.end());
    assert_pieces(expected, all, "the split point changed the result");
  }
}

void test_byte_by_byte_equals_whole() {
  const std::string stream = "[doubt]えっ？[sleepy]ねむい。。";
  const auto expected = run_whole(stream);

  SpeechSegmenter segmenter{60};
  for (const char c : stream) {
    segmenter.feed(std::string_view{&c, 1});
  }
  segmenter.flush();
  const auto actual = drain(segmenter);
  assert_pieces(expected, actual,
                "feeding one byte at a time changed the result");
}

void test_a_marker_split_across_deltas_still_works() {
  // Split inside a marker — "[hap" then "py]" — which does happen.
  SpeechSegmenter segmenter{60};
  segmenter.feed("[hap");
  segmenter.feed("py]やあ");
  segmenter.feed("。");
  segmenter.flush();
  const auto pieces = drain(segmenter);
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL(Expression::happy, pieces[0].first);
  TEST_ASSERT_EQUAL_STRING("やあ。", pieces[0].second.c_str());
}

void test_a_terminator_split_across_deltas_still_works() {
  // A three-byte sentence terminator arriving one byte at a time. The
  // sender does not split characters, but nothing here assumes that.
  const std::string maru = "\xE3\x80\x82";
  SpeechSegmenter segmenter{60};
  segmenter.feed("おわり");
  segmenter.feed(maru.substr(0, 1));
  segmenter.feed(maru.substr(1));
  const auto pieces = drain(segmenter);
  TEST_ASSERT_EQUAL_UINT32(1, pieces.size());
  TEST_ASSERT_EQUAL_STRING("おわり。", pieces[0].second.c_str());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_japanese_sentence_ends_at_the_terminator);
  RUN_TEST(test_fullwidth_exclamation_and_question_terminate);
  RUN_TEST(test_ascii_bang_and_question_terminate);
  RUN_TEST(test_a_newline_is_a_boundary_but_not_part_of_the_text);
  RUN_TEST(test_a_dot_inside_a_number_does_not_split);
  RUN_TEST(test_a_dot_before_a_space_terminates);
  RUN_TEST(test_a_dot_at_the_end_of_the_stream_terminates);
  RUN_TEST(test_leading_spaces_are_not_kept);
  RUN_TEST(test_a_marker_switches_the_expression);
  RUN_TEST(test_the_default_expression_is_neutral);
  RUN_TEST(test_the_expression_carries_across_sentences);
  RUN_TEST(test_a_marker_mid_sentence_forces_a_boundary);
  RUN_TEST(test_an_unknown_marker_stays_in_the_text);
  RUN_TEST(test_an_unclosed_marker_at_the_end_stays_in_the_text);
  RUN_TEST(test_uppercase_is_not_a_marker);
  RUN_TEST(test_every_expression_name_works_as_a_marker);
  RUN_TEST(test_a_long_sentence_is_cut_at_the_limit);
  RUN_TEST(test_the_cut_never_lands_inside_a_utf8_character);
  RUN_TEST(test_every_split_point_equals_whole);
  RUN_TEST(test_byte_by_byte_equals_whole);
  RUN_TEST(test_a_marker_split_across_deltas_still_works);
  RUN_TEST(test_a_terminator_split_across_deltas_still_works);
  return UNITY_END();
}
