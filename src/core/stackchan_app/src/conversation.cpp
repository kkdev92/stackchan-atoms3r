#include "stackchan/app/conversation.hpp"

#include "stackchan/domain/base64.hpp"
#include "stackchan/domain/json_scan.hpp"

namespace stackchan::app {
namespace {

// Reason string to outcome. An unrecognised reason fails rather than
// silently succeeding.
[[nodiscard]] ConversationTurn::Outcome outcome_of(std::string_view reason) noexcept {
  if (reason == "completed") {
    return ConversationTurn::Outcome::completed;
  }
  if (reason == "cancelled") {
    return ConversationTurn::Outcome::cancelled;
  }
  return ConversationTurn::Outcome::failed;
}

}  // namespace

void ConversationTurn::feed(std::string_view bytes) noexcept {
  if (outcome_ != Outcome::running) {
    return;  // anything arriving after the end is ignored
  }
  sse_.feed(bytes);
  std::string_view data;
  while (outcome_ == Outcome::running && sse_.next_event(data)) {
    handle_event(data);
  }
}

void ConversationTurn::handle_event(std::string_view data) noexcept {
  // Not part of the contract, but harmless when it turns up, so it is
  // consumed without comment.
  if (data == "[DONE]") {
    return;
  }

  std::int64_t version = 0;
  if (!domain::json_find_integer(data, "v", version) || version != 1) {
    ++dropped_;
    return;
  }
  std::string_view kind;
  if (!domain::json_find_raw_string(data, "kind", kind) || kind != "event") {
    ++dropped_;
    return;
  }
  std::string_view name;
  if (!domain::json_find_raw_string(data, "name", name)) {
    ++dropped_;
    return;
  }
  std::string_view payload;
  if (!domain::json_find_object(data, "payload", payload)) {
    // The contract has no event without a payload.
    ++dropped_;
    return;
  }

  if (name == "reply.audio") {
    handle_audio(payload);
    return;
  }

  if (name == "conversation.text") {
    std::string_view raw;
    if (!domain::json_find_raw_string(payload, "text", raw)) {
      ++dropped_;
      return;
    }
    const std::size_t len = domain::json_unescape(raw, text_.data(), text_.size());
    if (len == SIZE_MAX) {
      ++dropped_;
      return;
    }
    bool final_text = true;  // when omitted, treat the text as settled
    (void)domain::json_find_bool(payload, "final", final_text);
    listener_.on_recognized(std::string_view{text_.data(), len}, final_text);
    return;
  }

  if (name == "conversation.finished") {
    std::string_view reason;
    if (!domain::json_find_raw_string(payload, "reason", reason)) {
      ++dropped_;
      return;
    }
    const Outcome outcome = outcome_of(reason);
    domain::ErrorCode error = domain::ErrorCode::none;
    if (outcome == Outcome::failed) {
      // A failure the gateway already named keeps that name. One it did not
      // name is reported as internal, because leaving the code unset would
      // read as success.
      error = reported_error_ != domain::ErrorCode::none
                  ? reported_error_
                  : domain::ErrorCode::internal;
    }
    finish(outcome, error);
    return;
  }

  if (name == "error.raised") {
    std::string_view code_name;
    domain::ErrorCode code = domain::ErrorCode::internal;
    if (domain::json_find_raw_string(payload, "code", code_name)) {
      // Leave an unrecognised name as internal, rather than guessing at a
      // more specific meaning.
      (void)domain::parse_error_code(code_name, code);
    }
    reported_error_ = code;
    return;
  }

  // Events this version does not know about are skipped. Adding an event
  // without raising the envelope version is a compatible change, so old
  // firmware has to tolerate it.
}

void ConversationTurn::handle_audio(std::string_view payload) noexcept {
  // Audio at an unexpected rate cannot be played. Count it, drop it, and
  // let the stream continue.
  std::int64_t rate = 0;
  if (!domain::json_find_integer(payload, "rate", rate) || rate != 16000) {
    ++dropped_;
    return;
  }

  // A gap in the sequence means audio went missing. Rather than close the
  // hole and play something subtly wrong, abort.
  std::int64_t seq = -1;
  if (!domain::json_find_integer(payload, "seq", seq) || seq != expected_seq_) {
    finish(Outcome::failed, domain::ErrorCode::internal);
    return;
  }
  ++expected_seq_;

  // Text appears only on a sentence's first chunk; the expression and the
  // subtitle both come from it.
  std::string_view raw_text;
  if (domain::json_find_raw_string(payload, "text", raw_text)) {
    const std::size_t len =
        domain::json_unescape(raw_text, text_.data(), text_.size());
    if (len == SIZE_MAX) {
      // The string breaks the contract, so the event is unusable.
      ++dropped_;
      return;
    }
    segmenter_.feed(std::string_view{text_.data(), len});
    drain_segments();
  }

  std::string_view pcm_b64;
  if (!domain::json_find_raw_string(payload, "pcm", pcm_b64)) {
    ++dropped_;
    return;
  }
  // Decoded straight into an array of Sample. The samples are little-endian
  // 16-bit and so is the destination, so the bytes can be written as they
  // are.
  //
  // Reading an object's storage as unsigned char is allowed by the standard
  // and is the point of the cast; there is no second buffer to decode into
  // and no room for one.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* bytes_out = reinterpret_cast<std::uint8_t*>(pcm_.data());
  const std::size_t bytes = domain::base64_decode(
      pcm_b64, bytes_out, pcm_.size() * sizeof(ports::Sample));
  if (bytes == SIZE_MAX || bytes % sizeof(ports::Sample) != 0) {
    // Corrupt audio reaches the listener as noise, so stopping here is the
    // kinder failure.
    finish(Outcome::failed, domain::ErrorCode::internal);
    return;
  }

  bool last = false;
  (void)domain::json_find_bool(payload, "last", last);
  if (last) {
    // Emit the final sentence even if it never got a terminator.
    segmenter_.flush();
    drain_segments();
  }

  if (bytes > 0) {
    listener_.on_audio(pcm_.data(), bytes / sizeof(ports::Sample));
  }
}

void ConversationTurn::drain_segments() noexcept {
  domain::SpeechSegmenter::Segment segment;
  while (segmenter_.next_segment(segment)) {
    listener_.on_sentence(segment.expression, segment.text);
  }
}

void ConversationTurn::finish_input() noexcept {
  if (outcome_ != Outcome::running) {
    return;
  }
  sse_.finish();
  std::string_view data;
  while (outcome_ == Outcome::running && sse_.next_event(data)) {
    handle_event(data);
  }
  if (outcome_ != Outcome::running) {
    return;
  }
  // Closing without a completion event breaks the contract. If a reason
  // arrived earlier, keep it.
  finish(Outcome::failed, reported_error_ != domain::ErrorCode::none
                              ? reported_error_
                              : domain::ErrorCode::internal);
}

void ConversationTurn::abort(domain::ErrorCode code) noexcept {
  finish(code == domain::ErrorCode::cancelled ? Outcome::cancelled : Outcome::failed,
         code);
}

void ConversationTurn::finish(Outcome outcome, domain::ErrorCode code) noexcept {
  if (outcome_ != Outcome::running) {
    return;  // the end is reported once
  }
  outcome_ = outcome;
  code_ = code;
  listener_.on_finished(outcome, code);
}

}  // namespace stackchan::app
