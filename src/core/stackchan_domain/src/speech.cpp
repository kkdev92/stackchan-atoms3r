#include "stackchan/domain/speech.hpp"

#include <algorithm>
#include <cstring>

namespace stackchan::domain {
namespace {

// Multi-byte characters that end a sentence, in UTF-8.
struct Terminator {
  const char* bytes;
  std::size_t len;
};
constexpr std::array<Terminator, 3> kTerminators = {{
    {"\xE3\x80\x82", 3},  // 。
    {"\xEF\xBC\x81", 3},  // ！
    {"\xEF\xBC\x9F", 3},  // ？
}};

[[nodiscard]] bool is_marker_char(char c) noexcept { return c >= 'a' && c <= 'z'; }

[[nodiscard]] bool is_space(char c) noexcept { return c == ' ' || c == '\t'; }

// Length of a UTF-8 character from its first byte; 0 for a continuation.
[[nodiscard]] std::size_t utf8_length(unsigned char lead) noexcept {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead & 0xE0) == 0xC0) {
    return 2;
  }
  if ((lead & 0xF0) == 0xE0) {
    return 3;
  }
  if ((lead & 0xF8) == 0xF0) {
    return 4;
  }
  return 0;  // a continuation byte, not the start of a character
}

}  // namespace

SpeechSegmenter::SpeechSegmenter(std::size_t max_segment_bytes) noexcept
    : max_bytes_(
          std::clamp(max_segment_bytes, std::size_t{8}, kMaxSegmentCapacity)) {}

void SpeechSegmenter::feed(std::string_view text) noexcept {
  // Once drained, rewind the buffer to the start.
  if (fifo_read_ == fifo_write_) {
    fifo_read_ = 0;
    fifo_write_ = 0;
  }
  for (const char c : text) {
    put_char(c);
  }
}

// Whether a '.' ended a sentence is decided by the character after it.
// Returns true if c was consumed here, leaving the caller nothing to do.
bool SpeechSegmenter::resolve_pending_dot(char c) noexcept {
  dot_pending_ = false;
  if (is_space(c) || c == '\n' || c == '\r') {
    emit();
    // The whitespace itself is not wanted at the head of the next sentence.
    return is_space(c);
  }
  // Not a sentence end, so the '.' stays as an ordinary character.
  return false;
}

// One character inside a candidate marker; true if c was consumed. If the
// marker turns out not to be one, everything from '[' is emitted as text
// and false is returned, so the caller reprocesses c normally rather than
// this function recursing.
bool SpeechSegmenter::consume_into_marker(char c) noexcept {
  if (c == ']') {
    const std::string_view name{marker_.data() + 1, marker_len_ - 1};
    const auto parsed = parse_expression(name);
    marker_len_ = 0;
    if (parsed.has_value()) {
      // A marker also ends a sentence: emit what came before it under the
      // old expression, then switch.
      emit();
      expression_ = *parsed;
    } else {
      // An unrecognised marker stays as text, so ordinary brackets survive.
      append_text('[');
      for (const char m : name) {
        append_text(m);
      }
      append_text(']');
    }
    return true;
  }
  if (is_marker_char(c) && marker_len_ < kMaxMarkerBytes) {
    marker_[marker_len_++] = c;
    return true;
  }
  dump_marker_as_text();
  return false;
}

// Whether the buffer now ends with a multi-byte sentence terminator. It
// becomes true the moment the last byte arrives, however the input split.
bool SpeechSegmenter::tail_is_terminator() const noexcept {
  return std::any_of(kTerminators.begin(), kTerminators.end(),
                     [this](const Terminator& t) {
                       return pending_len_ >= t.len &&
                              std::memcmp(pending_.data() + pending_len_ - t.len,
                                          t.bytes, t.len) == 0;
                     });
}

void SpeechSegmenter::put_char(char c) noexcept {
  if (dot_pending_ && resolve_pending_dot(c)) {
    return;
  }
  if (marker_len_ > 0 && consume_into_marker(c)) {
    return;
  }

  if (c == '[') {
    marker_[0] = '[';
    marker_len_ = 1;
    return;
  }
  if (c == '\n' || c == '\r') {
    emit();
    return;
  }
  // Do not accumulate whitespace at the start of a sentence.
  if (pending_len_ == 0 && is_space(c)) {
    return;
  }

  append_text(c);

  if (c == '!' || c == '?') {
    emit();
    return;
  }
  if (c == '.') {
    // Look at the next character before deciding, so "3.14" survives.
    dot_pending_ = true;
    return;
  }
  if (tail_is_terminator()) {
    emit();
  }
}

void SpeechSegmenter::append_text(char c) noexcept {
  pending_[pending_len_++] = c;

  if (pending_len_ < max_bytes_) {
    return;
  }

  // At the limit. The cut must not land inside a UTF-8 character, so if the
  // buffer ends mid-character, cut before that character starts.
  std::size_t lead = pending_len_;
  while (lead > 0 &&
         utf8_length(static_cast<unsigned char>(pending_[lead - 1])) == 0) {
    --lead;
  }
  std::size_t cut = pending_len_;
  if (lead > 0) {
    const std::size_t char_len =
        utf8_length(static_cast<unsigned char>(pending_[lead - 1]));
    if (lead - 1 + char_len > pending_len_) {
      cut = lead - 1;  // the last character is incomplete; cut before it
    }
  }

  if (cut == 0) {
    // The whole buffer is one partial character. This is why the limit is
    // never allowed below eight bytes.
    return;
  }

  // Emit the first part and move the unfinished remainder to the front.
  const std::size_t rest = pending_len_ - cut;
  const std::size_t keep_len = rest;
  std::array<char, 8> keep{};
  std::memcpy(keep.data(), pending_.data() + cut, keep_len);
  pending_len_ = cut;
  emit();
  std::memcpy(pending_.data(), keep.data(), keep_len);
  pending_len_ = keep_len;
}

void SpeechSegmenter::emit() noexcept {
  dot_pending_ = false;
  if (pending_len_ == 0) {
    return;
  }

  const std::size_t need = 2 + pending_len_;
  if (fifo_write_ + need > fifo_.size()) {
    ++dropped_;
    pending_len_ = 0;
    return;
  }
  fifo_[fifo_write_] = static_cast<char>(pending_len_);
  fifo_[fifo_write_ + 1] = static_cast<char>(expression_);
  std::memcpy(fifo_.data() + fifo_write_ + 2, pending_.data(), pending_len_);
  fifo_write_ += need;
  pending_len_ = 0;
}

void SpeechSegmenter::dump_marker_as_text() noexcept {
  const std::size_t len = marker_len_;
  marker_len_ = 0;
  for (std::size_t i = 0; i < len; ++i) {
    append_text(marker_[i]);
  }
}

void SpeechSegmenter::flush() noexcept {
  if (fifo_read_ == fifo_write_) {
    fifo_read_ = 0;
    fifo_write_ = 0;
  }
  if (marker_len_ > 0) {
    // A marker that never closed is emitted as text.
    dump_marker_as_text();
  }
  emit();
}

bool SpeechSegmenter::next_segment(Segment& out) noexcept {
  if (fifo_read_ == fifo_write_) {
    return false;
  }
  const auto len = static_cast<std::size_t>(
      static_cast<unsigned char>(fifo_[fifo_read_]));
  out.expression = static_cast<Expression>(fifo_[fifo_read_ + 1]);
  out.text = std::string_view{fifo_.data() + fifo_read_ + 2, len};
  fifo_read_ += 2 + len;
  return true;
}

}  // namespace stackchan::domain
