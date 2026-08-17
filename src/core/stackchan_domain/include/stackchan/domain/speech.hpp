#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/domain/expression.hpp"

// Segmenting speech: cutting streamed text into sentences, each carrying an
// expression.
//
// What this solves
// ----------------
// The face should change expression as the reply is spoken, and it should
// do so without asking the gateway anything extra. The way to get that is
// to let the reply text carry markers, and to interpret them here, sentence
// by sentence:
//
//   [happy]That is a good question. [neutral]The answer is 42.
//
// The interpretation happens **while the response is still streaming**. A
// sentence is emitted as soon as its end arrives, so speech and expression
// start before the full response exists.
//
// Input arrives as deltas
// -----------------------
// SSE deltas split at arbitrary points, including **inside a marker**:
// "[hap" followed by "py]hello". As with SseReader, the tests fix the
// property that no split changes the result.
//
// The rules
// ---------
// - a recognised marker switches expression and is removed from the text
// - a marker also ends the current sentence: text before it is emitted
//   with the expression that was in force
// - **an unrecognised marker stays as literal text**, so "[1, 2, 3]"
//   survives intact
// - sentences end at 。！？!? and at newlines. An ASCII '.' ends one only
//   when followed by whitespace or the end of input, which keeps "3.14"
//   and "1." together
// - an over-long sentence is cut at max_segment_bytes, never mid-character
//   in UTF-8. The gateway guarantees that property on input; it is
//   preserved on output
//
// All buffers are fixed size and never grow.

namespace stackchan::domain {

class SpeechSegmenter {
 public:
  struct Segment {
    Expression expression = Expression::neutral;
    std::string_view text;
  };

  // Longest sentence, in bytes — bytes rather than characters, since the
  // text is UTF-8 and the cut must not land inside a character.
  //
  // 60 is about as much as a speech engine handles comfortably in one
  // request. Raising it makes the first sound arrive later, because a
  // sentence is only spoken once it is complete.
  explicit SpeechSegmenter(std::size_t max_segment_bytes = 60) noexcept;

  ~SpeechSegmenter() = default;
  SpeechSegmenter(const SpeechSegmenter&) = delete;
  SpeechSegmenter& operator=(const SpeechSegmenter&) = delete;
  SpeechSegmenter(SpeechSegmenter&&) = delete;
  SpeechSegmenter& operator=(SpeechSegmenter&&) = delete;

  // Feed a delta of text. Any split is acceptable.
  void feed(std::string_view text) noexcept;

  // End of stream. Flushes a partial sentence and any pending marker.
  void flush() noexcept;

  // Take one finished sentence, or false if none is ready. The returned
  // text stays valid **until the next call to feed or flush**.
  [[nodiscard]] bool next_segment(Segment& out) noexcept;

  // Sentences discarded for want of room. Anything but zero means the
  // consumer is not keeping up.
  [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }

  // The current expression: whatever the most recent marker selected.
  [[nodiscard]] Expression current_expression() const noexcept { return expression_; }

 private:
  void put_char(char c) noexcept;
  [[nodiscard]] bool resolve_pending_dot(char c) noexcept;
  [[nodiscard]] bool consume_into_marker(char c) noexcept;
  [[nodiscard]] bool tail_is_terminator() const noexcept;
  void append_text(char c) noexcept;
  void emit() noexcept;
  void dump_marker_as_text() noexcept;

  // The limits are fixed by the design. Callers choose only the sentence
  // length.
  static constexpr std::size_t kMaxSegmentCapacity = 200;
  // Longest marker: '[' plus the longest expression name (neutral, 7).
  static constexpr std::size_t kMaxMarkerBytes = 1 + 7;

  std::size_t max_bytes_;
  Expression expression_ = Expression::neutral;

  std::array<char, kMaxSegmentCapacity> pending_{};
  std::size_t pending_len_ = 0;

  // A candidate marker, from '[' until it closes. Once invalid, it is
  // emitted back out as ordinary text.
  std::array<char, kMaxMarkerBytes + 1> marker_{};
  std::size_t marker_len_ = 0;

  // Set after an ASCII '.', while the next character decides whether it
  // ended a sentence.
  bool dot_pending_ = false;

  // Where finished sentences wait, packed as [length][expression][body].
  std::array<char, 512> fifo_{};
  std::size_t fifo_write_ = 0;
  std::size_t fifo_read_ = 0;
  std::size_t dropped_ = 0;
};

}  // namespace stackchan::domain
