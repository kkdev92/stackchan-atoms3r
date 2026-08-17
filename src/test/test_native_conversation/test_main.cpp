#include <unity.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stackchan/app/conversation.hpp"

using stackchan::app::ConversationTurn;
using stackchan::domain::ErrorCode;
using stackchan::domain::Expression;

namespace {

// ----------------------------------------------------------- helpers

std::string b64(const std::uint8_t* data, std::size_t size) {
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

// Recognisable PCM: consecutive 16-bit samples from a seed.
std::vector<std::int16_t> ramp(std::int16_t seed, std::size_t count) {
  std::vector<std::int16_t> out(count);
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = static_cast<std::int16_t>(seed + static_cast<std::int16_t>(i));
  }
  return out;
}

std::string sse(const std::string& json) { return "data: " + json + "\n\n"; }

std::string audio_event(int seq, const char* text, const std::vector<std::int16_t>& pcm,
                        bool last, int rate = 16000) {
  std::string json = R"({"v":1,"kind":"event","name":"reply.audio","payload":{)";
  json += "\"seq\":" + std::to_string(seq);
  if (text != nullptr) {
    json += ",\"text\":\"" + std::string{text} + "\"";
  }
  json += ",\"rate\":" + std::to_string(rate);
  json += ",\"pcm\":\"" +
          b64(reinterpret_cast<const std::uint8_t*>(pcm.data()),
              pcm.size() * sizeof(std::int16_t)) +
          "\"";
  json += std::string{",\"last\":"} + (last ? "true" : "false");
  json += "}}";
  return sse(json);
}

// Records the instructions as strings, so the order can be compared too.
class Recorder final : public ConversationTurn::Listener {
 public:
  void on_recognized(std::string_view text, bool final) override {
    log.push_back("rec:" + std::string{final ? "1" : "0"} + ":" + std::string{text});
  }
  void on_sentence(Expression expression, std::string_view text) override {
    log.push_back("sent:" + std::string{to_string(expression)} + ":" +
                  std::string{text});
  }
  void on_audio(const stackchan::ports::Sample* samples, std::size_t count) override {
// Comparing every sample would be unwieldy, so the count and the end
// values stand in for identity.
    log.push_back("audio:" + std::to_string(count) + ":" +
                  std::to_string(samples[0]) + ":" +
                  std::to_string(samples[count - 1]));
  }
  void on_finished(ConversationTurn::Outcome outcome, ErrorCode code) override {
    log.push_back("fin:" + std::to_string(static_cast<int>(outcome)) + ":" +
                  std::string{to_string(code)});
  }

  std::vector<std::string> log;
};

// A complete, well-formed exchange: the recognised text, a happy sentence
// in two audio chunks, a sad sentence, then completion.
//
// The text is Japanese so that the multi-byte path is the one being tested.
// A stream can be split anywhere, including between two bytes of the same
// character, and this is where that has to survive.
std::string happy_stream() {
  std::string s;
  s += sse(R"({"v":1,"kind":"event","name":"conversation.started","payload":{"conversation_id":"B-1"}})");
  s += sse(R"({"v":1,"kind":"event","name":"conversation.text","payload":{"text":"こんにちは","final":true}})");
  s += audio_event(0, "[happy]やあ。", ramp(100, 96), false);
  s += audio_event(1, nullptr, ramp(2000, 64), false);  // same sentence, continued
  s += audio_event(2, "[sad]雨だよ。", ramp(-500, 48), true);
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");
  return s;
}

std::vector<std::string> run_whole(const std::string& stream) {
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(stream);
  turn.finish_input();
  return recorder.log;
}

// ------------------------------------------------------ the normal case

void test_a_full_turn_produces_the_expected_instruction_sequence() {
  const auto log = run_whole(happy_stream());

  const std::vector<std::string> expected = {
      "rec:1:こんにちは",
      "sent:happy:やあ。",
      "audio:96:100:195",     // the ends of ramp(100, 96)
      "audio:64:2000:2063",   // continued; no new sentence is announced
      "sent:sad:雨だよ。",
      "audio:48:-500:-453",
      "fin:1:none",           // 1 = completed
  };
  TEST_ASSERT_EQUAL_UINT32(expected.size(), log.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), log[i].c_str());
  }
}

void test_every_split_point_yields_the_same_instructions() {
// The point of these: wherever the transport splits the bytes, the
// instructions that come out are identical.
  const std::string stream = happy_stream();
  const auto expected = run_whole(stream);

  // Trying every split point would take a while over several kilobytes, so
  // a prime step is used to cover the whole range.
  for (std::size_t split = 1; split < stream.size(); split += 7) {
    Recorder recorder;
    ConversationTurn turn{recorder};
    turn.feed(std::string_view{stream}.substr(0, split));
    turn.feed(std::string_view{stream}.substr(split));
    turn.finish_input();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        expected.size(), recorder.log.size(),
        "the split point changed how many instructions came out");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), recorder.log[i].c_str());
    }
  }
}

void test_finished_without_terminator_text_is_flushed_by_last() {
  // A sentence that never gets a terminator is still emitted, because the
  // event says it is the last one.
  std::string s;
  s += audio_event(0, "[doubt]うーん", ramp(7, 16), true);
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");

  const auto log = run_whole(s);
  TEST_ASSERT_EQUAL_UINT32(3, log.size());
  TEST_ASSERT_EQUAL_STRING("sent:doubt:うーん", log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("audio:16:7:22", log[1].c_str());
  TEST_ASSERT_EQUAL_STRING("fin:1:none", log[2].c_str());
}

void test_cancelled_reason_maps_to_the_cancelled_outcome() {
  const auto log = run_whole(
      sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"cancelled"}})"));
  TEST_ASSERT_EQUAL_UINT32(1, log.size());
  TEST_ASSERT_EQUAL_STRING("fin:2:none", log[0].c_str());  // 2 = cancelled
}

// ------------------------------ contract violations and foreign events

void test_a_sequence_gap_fails_instead_of_playing_with_a_hole() {
  std::string s;
  s += audio_event(0, "[happy]一。", ramp(1, 8), false);
  s += audio_event(2, nullptr, ramp(2, 8), false);  // sequence 1 is missing
  s += audio_event(3, nullptr, ramp(3, 8), false);

  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);

  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::failed, turn.outcome());
  TEST_ASSERT_EQUAL(ErrorCode::internal, turn.code());
  // Everything before the gap plays; nothing after it does.
  TEST_ASSERT_EQUAL_STRING("sent:happy:一。", recorder.log[0].c_str());
  TEST_ASSERT_EQUAL_STRING("audio:8:1:8", recorder.log[1].c_str());
  TEST_ASSERT_EQUAL_STRING("fin:3:internal", recorder.log[2].c_str());
  TEST_ASSERT_EQUAL_UINT32(3, recorder.log.size());
}

void test_a_wrong_rate_is_counted_and_skipped() {
  std::string s;
  s += audio_event(0, "[happy]一。", ramp(1, 8), false, 8000);  // unsupported rate
  s += audio_event(0, "[happy]二。", ramp(9, 8), true);         // sequence unchanged
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");

  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);
  TEST_ASSERT_EQUAL_UINT32(1, turn.dropped_events());
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::completed, turn.outcome());
  TEST_ASSERT_EQUAL_STRING("sent:happy:二。", recorder.log[0].c_str());
}

void test_legacy_done_and_unknown_events_are_ignored() {
  std::string s;
  s += "data: [DONE]\n\n";  // outside the contract, but harmless
  s += sse(R"({"v":1,"kind":"event","name":"future.thing","payload":{"x":1}})");
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");

  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);
  TEST_ASSERT_EQUAL_UINT32(0, turn.dropped_events());  // neither counts as dropped
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::completed, turn.outcome());
}

void test_a_malformed_event_is_counted_and_the_stream_continues() {
  std::string s;
  s += "data: {not json at all\n\n";
  s += sse(R"({"v":2,"kind":"event","name":"x","payload":{}})");  // unknown version
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");

  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);
  TEST_ASSERT_EQUAL_UINT32(2, turn.dropped_events());
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::completed, turn.outcome());
}

void test_error_raised_then_closed_carries_the_reported_code() {
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(sse(
      R"({"v":1,"kind":"event","name":"error.raised","payload":{"code":"unavailable","message":"llm down","retryable":true}})"));
  turn.finish_input();

  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::failed, turn.outcome());
  TEST_ASSERT_EQUAL(ErrorCode::unavailable, turn.code());
}

void test_closing_without_finished_is_a_contract_violation() {
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(sse(R"({"v":1,"kind":"event","name":"conversation.text","payload":{"text":"x"}})"));
  turn.finish_input();
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::failed, turn.outcome());
  TEST_ASSERT_EQUAL(ErrorCode::internal, turn.code());
}

void test_abort_wins_and_later_bytes_are_ignored() {
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(audio_event(0, "[happy]一。", ramp(1, 8), false));
  turn.abort(ErrorCode::timeout);
  turn.feed(audio_event(1, nullptr, ramp(2, 8), true));  // arriving late
  turn.finish_input();

  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::failed, turn.outcome());
  TEST_ASSERT_EQUAL(ErrorCode::timeout, turn.code());
  TEST_ASSERT_EQUAL_STRING("fin:3:timeout", recorder.log.back().c_str());
  // No further audio is delivered after the abort.
  TEST_ASSERT_EQUAL_UINT32(3, recorder.log.size());
}

void test_abort_with_cancelled_maps_to_the_cancelled_outcome() {
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.abort(ErrorCode::cancelled);
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::cancelled, turn.outcome());
}

void test_a_unicode_escape_in_text_drops_the_event_but_not_the_stream() {
  // An escape the contract does not permit. That event is dropped and the
  // stream carries on. (The backslash is added at runtime so the test's own
  // helpers do not consume it.)
  std::string bad = R"({"v":1,"kind":"event","name":"reply.audio","payload":{"seq":0,"text":"a)";
  bad += '\\';
  bad += R"(u3042","rate":16000,"pcm":"TWFu","last":false}})";
  std::string s;
  s += sse(bad);
  s += sse(R"({"v":1,"kind":"event","name":"conversation.finished","payload":{"reason":"completed"}})");

  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);
  TEST_ASSERT_EQUAL_UINT32(1, turn.dropped_events());
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::completed, turn.outcome());
}

void test_corrupt_pcm_fails_the_turn() {
  // Corrupt audio is not played as noise.
  std::string s = sse(
      R"({"v":1,"kind":"event","name":"reply.audio","payload":{"seq":0,"rate":16000,"pcm":"!!invalid!!","last":false}})");
  Recorder recorder;
  ConversationTurn turn{recorder};
  turn.feed(s);
  TEST_ASSERT_EQUAL(ConversationTurn::Outcome::failed, turn.outcome());
  TEST_ASSERT_EQUAL(ErrorCode::internal, turn.code());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_full_turn_produces_the_expected_instruction_sequence);
  RUN_TEST(test_every_split_point_yields_the_same_instructions);
  RUN_TEST(test_finished_without_terminator_text_is_flushed_by_last);
  RUN_TEST(test_cancelled_reason_maps_to_the_cancelled_outcome);
  RUN_TEST(test_a_sequence_gap_fails_instead_of_playing_with_a_hole);
  RUN_TEST(test_a_wrong_rate_is_counted_and_skipped);
  RUN_TEST(test_legacy_done_and_unknown_events_are_ignored);
  RUN_TEST(test_a_malformed_event_is_counted_and_the_stream_continues);
  RUN_TEST(test_error_raised_then_closed_carries_the_reported_code);
  RUN_TEST(test_closing_without_finished_is_a_contract_violation);
  RUN_TEST(test_abort_wins_and_later_bytes_are_ignored);
  RUN_TEST(test_abort_with_cancelled_maps_to_the_cancelled_outcome);
  RUN_TEST(test_a_unicode_escape_in_text_drops_the_event_but_not_the_stream);
  RUN_TEST(test_corrupt_pcm_fails_the_turn);
  return UNITY_END();
}
