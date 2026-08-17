#include <unity.h>

#include <string>
#include <vector>

#include "stackchan/domain/sse.hpp"

using stackchan::domain::SseReader;

namespace {

// Drain every event currently available.
std::vector<std::string> drain(SseReader& reader) {
  std::vector<std::string> out;
  std::string_view data;
  while (reader.next_event(data)) {
    out.emplace_back(data);
  }
  return out;
}

// The events produced when the whole stream is fed at once, used as the
// reference to compare against.
std::vector<std::string> parse_whole(std::string_view stream) {
  SseReader reader;
  reader.feed(stream);
  reader.finish();
  return drain(reader);
}

// A stream shaped like what the gateway actually sends: a keep-alive
// comment, UTF-8 text, and a terminating marker.
const std::string kGatewayLike =
    ": keep-alive\n\n"
    "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\"}}]}\n\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"こんにちは\"}}]}\n\n"
    ": keep-alive\n\n"
    "data: {\"choices\":[{\"delta\":{\"content\":\"、世界。\"}}]}\n\n"
    "data: {\"choices\":[{\"finish_reason\":\"stop\"}]}\n\n"
    "data: [DONE]\n\n";

void test_a_single_event() {
  const auto events = parse_whole("data: hello\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("hello", events[0].c_str());
}

void test_the_space_after_the_colon_is_optional() {
  // "data:x" and "data: x" mean the same thing.
  const auto events = parse_whole("data:x\n\ndata: x\n\n");
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("x", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("x", events[1].c_str());
}

void test_done_passes_through_verbatim() {
  // Interpreting the end-of-stream marker is the caller's job; it passes
  // through here unchanged.
  const auto events = parse_whole("data: [DONE]\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("[DONE]", events[0].c_str());
}

void test_comments_produce_no_events() {
  // Keep-alives arrive as comment lines.
  const auto events = parse_whole(": keep-alive\n\n: keep-alive\n\n");
  TEST_ASSERT_EQUAL_UINT32(0, events.size());
}

void test_a_comment_between_events_does_not_break_them() {
  const auto events = parse_whole("data: a\n\n: keep-alive\n\ndata: b\n\n");
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("a", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("b", events[1].c_str());
}

void test_crlf_line_endings() {
  const auto events = parse_whole("data: a\r\n\r\ndata: b\r\n\r\n");
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("a", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("b", events[1].c_str());
}

void test_cr_only_line_endings() {
  // A lone CR also ends a line.
  const auto events = parse_whole("data: a\r\rdata: b\r\r");
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("a", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("b", events[1].c_str());
}

void test_multiple_data_lines_join_with_lf() {
  // Multiple data lines are joined with LF.
  const auto events = parse_whole("data: line1\ndata: line2\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("line1\nline2", events[0].c_str());
}

void test_other_fields_are_ignored() {
  const auto events = parse_whole("event: message\nid: 42\nretry: 100\ndata: x\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("x", events[0].c_str());
}

void test_an_event_without_data_is_not_emitted() {
  const auto events = parse_whole("event: ping\nid: 1\n\n");
  TEST_ASSERT_EQUAL_UINT32(0, events.size());
}

void test_empty_data_is_a_real_event() {
  // A bare "data:" is still a data field, with an empty value. It is not
  // discarded.
  const auto events = parse_whole("data:\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("", events[0].c_str());
}

void test_utf8_passes_byte_exact() {
  const auto events = parse_whole("data: こんにちは、世界\n\n");
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("こんにちは、世界", events[0].c_str());
}

void test_two_events_in_one_feed_are_both_kept() {
  // Several events in one block are all kept.
  SseReader reader;
  reader.feed("data: a\n\ndata: b\n\n");
  const auto events = drain(reader);
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("a", events[0].c_str());
  TEST_ASSERT_EQUAL_STRING("b", events[1].c_str());
}

void test_byte_by_byte_equals_whole() {
  // First demonstration that the split does not matter: one byte at a time.
  const auto expected = parse_whole(kGatewayLike);

  SseReader reader;
  for (const char c : kGatewayLike) {
    reader.feed(std::string_view{&c, 1});
  }
  reader.finish();
  const auto actual = drain(reader);

  TEST_ASSERT_EQUAL_UINT32(expected.size(), actual.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), actual[i].c_str());
  }
}

void test_every_split_point_equals_whole() {
  // Second demonstration: split in two at every possible position. A reader
  // that assumes whole lines loses an entire JSON document when a split
  // lands mid-line.
  const auto expected = parse_whole(kGatewayLike);

  for (std::size_t split = 1; split < kGatewayLike.size(); ++split) {
    SseReader reader;
    reader.feed(std::string_view{kGatewayLike}.substr(0, split));
    // Draining part-way through is safe, which is how the real receive loop
    // behaves.
    auto first = drain(reader);
    reader.feed(std::string_view{kGatewayLike}.substr(split));
    reader.finish();
    const auto second = drain(reader);

    first.insert(first.end(), second.begin(), second.end());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        expected.size(), first.size(),
        "the split point changed how many events came out");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), first[i].c_str());
    }
  }
}

void test_an_oversized_event_is_counted_and_dropped() {
  // An event too large to fit is counted and discarded, never silently
  // truncated.
  SseReader reader;
  const std::string huge = "data: " + std::string(SseReader::kMaxEventBytes + 100, 'x') +
                           "\n\ndata: after\n\n";
  reader.feed(huge);
  const auto events = drain(reader);

  TEST_ASSERT_EQUAL_UINT32(1, reader.dropped());
  // Events after it still arrive: one oversized event does not kill the
  // stream.
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("after", events[0].c_str());
}

void test_finish_flushes_a_stream_without_trailing_newlines() {
  // Insurance against a sender that does not end tidily. The specification
  // dispatches an event implicitly at the end of the stream.
  SseReader reader;
  reader.feed("data: tail");
  TEST_ASSERT_EQUAL_UINT32(0, drain(reader).size());
  reader.finish();
  const auto events = drain(reader);
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("tail", events[0].c_str());
}

void test_undrained_overflow_is_counted_not_overwritten() {
  // What happens when the caller stops draining. Events are counted and
  // discarded rather than overwriting ones not yet read. The sizes follow
  // from the limits: the queue holds twice the maximum event size, so two
  // events of three-quarter size fit and a third does not.
  SseReader reader;
  const std::string big(SseReader::kMaxEventBytes * 3 / 4, 'a');
  reader.feed("data: " + big + "\n\n");
  reader.feed("data: " + big + "\n\n");
  reader.feed("data: " + big + "\n\n");  // a third one, with no room left

  TEST_ASSERT_EQUAL_UINT32(1, reader.dropped());
  const auto events = drain(reader);
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_UINT32(big.size(), events[0].size());
}

void test_the_gateway_like_stream_yields_the_expected_sequence() {
  const auto events = parse_whole(kGatewayLike);
  TEST_ASSERT_EQUAL_UINT32(5, events.size());
  TEST_ASSERT_TRUE(events[0].find("assistant") != std::string::npos);
  TEST_ASSERT_TRUE(events[1].find("こんにちは") != std::string::npos);
  TEST_ASSERT_TRUE(events[2].find("、世界。") != std::string::npos);
  TEST_ASSERT_TRUE(events[3].find("stop") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("[DONE]", events[4].c_str());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_single_event);
  RUN_TEST(test_the_space_after_the_colon_is_optional);
  RUN_TEST(test_done_passes_through_verbatim);
  RUN_TEST(test_comments_produce_no_events);
  RUN_TEST(test_a_comment_between_events_does_not_break_them);
  RUN_TEST(test_crlf_line_endings);
  RUN_TEST(test_cr_only_line_endings);
  RUN_TEST(test_multiple_data_lines_join_with_lf);
  RUN_TEST(test_other_fields_are_ignored);
  RUN_TEST(test_an_event_without_data_is_not_emitted);
  RUN_TEST(test_empty_data_is_a_real_event);
  RUN_TEST(test_utf8_passes_byte_exact);
  RUN_TEST(test_two_events_in_one_feed_are_both_kept);
  RUN_TEST(test_byte_by_byte_equals_whole);
  RUN_TEST(test_every_split_point_equals_whole);
  RUN_TEST(test_an_oversized_event_is_counted_and_dropped);
  RUN_TEST(test_finish_flushes_a_stream_without_trailing_newlines);
  RUN_TEST(test_undrained_overflow_is_counted_not_overwritten);
  RUN_TEST(test_the_gateway_like_stream_yields_the_expected_sequence);
  return UNITY_END();
}
