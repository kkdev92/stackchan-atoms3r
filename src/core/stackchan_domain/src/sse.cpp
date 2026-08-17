#include "stackchan/domain/sse.hpp"

#include <cstring>

namespace stackchan::domain {
namespace {

// Split a field into name and value. With no ':' the whole line is the
// name, as the specification requires. One leading space in the value is
// stripped, so "data: x" and "data:x" mean the same thing.
void split_field(std::string_view line, std::string_view& name,
                 std::string_view& value) noexcept {
  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos) {
    name = line;
    value = {};
    return;
  }
  name = line.substr(0, colon);
  value = line.substr(colon + 1);
  if (!value.empty() && value.front() == ' ') {
    value.remove_prefix(1);
  }
}

}  // namespace

void SseReader::feed(std::string_view bytes) noexcept {
  // Rewind the queue once it has been drained. Doing it here is what keeps
  // the view returned by next_event valid until the next feed.
  if (fifo_read_ == fifo_write_) {
    fifo_read_ = 0;
    fifo_write_ = 0;
  }

  for (const char c : bytes) {
    if (c == '\r') {
      // A lone CR also ends a line. An LF immediately after it is the
      // second half of a CRLF and is skipped.
      take_line(std::string_view{line_.data(), line_len_});
      line_len_ = 0;
      line_overflow_ = false;
      last_was_cr_ = true;
      continue;
    }
    if (c == '\n') {
      if (last_was_cr_) {
        // The LF of a CRLF; the line already ended at the CR.
        last_was_cr_ = false;
        continue;
      }
      take_line(std::string_view{line_.data(), line_len_});
      line_len_ = 0;
      line_overflow_ = false;
      continue;
    }
    last_was_cr_ = false;

    if (line_len_ < line_.size()) {
      line_[line_len_++] = c;
    } else {
      // The line does not fit. Discard up to the end of it, but remember
      // that it happened: if it was a data line, the event is incomplete
      // and has to be thrown away rather than delivered short.
      line_overflow_ = true;
    }
  }
}

void SseReader::take_line(std::string_view line) noexcept {
  if (line.empty() && !line_overflow_) {
    // A blank line dispatches the event.
    dispatch();
    return;
  }

  if (!line.empty() && line.front() == ':') {
    // A comment. Keep-alives arrive as these.
    return;
  }

  std::string_view name;
  std::string_view value;
  split_field(line, name, value);
  // split_field assigns name through the reference. cppcheck does not follow
  // that, so it still believes name is the empty view declared just above
  // and calls the comparison already decided.
  // cppcheck-suppress knownConditionTrueFalse
  if (name != "data") {
    // event: and id: are not used here, so they are discarded.
    return;
  }

  if (line_overflow_) {
    // Mixing in a truncated data line would produce broken JSON, so the
    // whole event is marked for discarding.
    event_overflow_ = true;
    event_has_data_ = true;
    return;
  }

  // Second and later data lines are joined with LF, per the specification.
  const std::size_t need = value.size() + (event_has_data_ ? 1 : 0);
  if (event_len_ + need > event_.size()) {
    event_overflow_ = true;
    event_has_data_ = true;
    return;
  }
  if (event_has_data_) {
    event_[event_len_++] = '\n';
  }
  std::memcpy(event_.data() + event_len_, value.data(), value.size());
  event_len_ += value.size();
  event_has_data_ = true;
}

void SseReader::dispatch() noexcept {
  const bool had_data = event_has_data_;
  const bool overflow = event_overflow_;
  const std::size_t len = event_len_;

  event_len_ = 0;
  event_has_data_ = false;
  event_overflow_ = false;

  if (!had_data) {
    // An event with no data — a bare comment, say — is not dispatched.
    return;
  }
  if (overflow) {
    ++dropped_;
    return;
  }

  // Queued as [2-byte length][body].
  const std::size_t need = 2 + len;
  if (fifo_write_ + need > fifo_.size()) {
    // The consumer is not keeping up. Count the loss rather than silently
    // overwriting an event it has not read yet.
    ++dropped_;
    return;
  }
  fifo_[fifo_write_] = static_cast<char>(len & 0xFF);
  fifo_[fifo_write_ + 1] = static_cast<char>((len >> 8) & 0xFF);
  std::memcpy(fifo_.data() + fifo_write_ + 2, event_.data(), len);
  fifo_write_ += need;
}

bool SseReader::next_event(std::string_view& out_data) noexcept {
  if (fifo_read_ == fifo_write_) {
    return false;
  }
  const auto low = static_cast<unsigned char>(fifo_[fifo_read_]);
  const auto high = static_cast<unsigned char>(fifo_[fifo_read_ + 1]);
  const std::size_t len = static_cast<std::size_t>(low) |
                          (static_cast<std::size_t>(high) << 8);
  out_data = std::string_view{fifo_.data() + fifo_read_ + 2, len};
  fifo_read_ += 2 + len;
  return true;
}

void SseReader::finish() noexcept {
  // Catches a final line with no trailing newline, and a final event with
  // no blank line after it.
  if (line_len_ > 0 || line_overflow_) {
    take_line(std::string_view{line_.data(), line_len_});
    line_len_ = 0;
    line_overflow_ = false;
  }
  dispatch();
}

}  // namespace stackchan::domain
