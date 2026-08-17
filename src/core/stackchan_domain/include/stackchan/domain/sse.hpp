#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Reading Server-Sent Events: turning a byte stream into events.
//
// What this solves
// ----------------
// A TCP stream arrives in whatever pieces the network produces. An event
// can be split down the middle, and several events can turn up in one
// block. A reader that assumes one event per read will silently lose
// whichever ones fall the wrong way, and the device just goes quiet.
//
// So this reader assembles correctly **however the stream is chopped up**.
// One byte at a time, or five events at once, gives the same result. That
// property is what the tests fix, and it is why the sender needs no special
// framing.
//
// Where the rules come from
// -------------------------
// The WHATWG HTML Standard, Server-sent events:
//
//   - lines may end with CR, LF or CRLF
//   - a blank line dispatches the event
//   - a line beginning with ':' is a comment, used for keep-alives
//   - fields are "name: value"; one leading space in the value is stripped
//   - multiple data lines are joined with LF
//
// And what the gateway actually emits:
//
//   - "data: {json}\n\n", raw UTF-8, never split mid-character
//   - keep-alives as the comment line ": keep-alive\n\n"
//   - "data: [DONE]\n\n" to finish. Interpreting that string is the
//     caller's job, not this reader's
//
// HTTP chunked encoding is assumed to be gone by the time bytes arrive,
// because the HTTP client strips it. Feed this raw TCP instead and the
// hexadecimal chunk-size lines will end up in the output.
//
// Memory
// ------
// Fixed buffers that never grow. An event that does not fit is **counted
// and discarded**: passing on a silently truncated JSON document would
// break the parser downstream (the same reasoning as JsonWriter).

namespace stackchan::domain {

class SseReader {
 public:
  // Largest event accepted, matching the 8192 bytes the contract states for
  // an assembled event (docs/api/device-interface.md).
  //
  // Do not lower this. A legitimate audio event is already large: 4096
  // bytes of raw PCM become 5462 after base64, around 5.7 KB with the
  // envelope. Set it below that and ordinary events start being discarded
  // as oversized, while smaller ones still get through — so the sequence
  // numbers jump and the conversation fails for a reason that looks
  // nothing like "the buffer was too small".
  //
  // The contract's limit and this limit have to be the same number.
  static constexpr std::size_t kMaxEventBytes = 8192;

  SseReader() noexcept = default;
  ~SseReader() = default;

  SseReader(const SseReader&) = delete;
  SseReader& operator=(const SseReader&) = delete;
  SseReader(SseReader&&) = delete;
  SseReader& operator=(SseReader&&) = delete;

  // Feed a received block. Any split is acceptable.
  //
  // The bytes are copied out, so the caller may reuse its buffer at once.
  void feed(std::string_view bytes) noexcept;

  // Take the data of one assembled event, or false if none is ready.
  //
  // The returned view stays valid **until the next call to feed**. Multiple
  // data lines arrive joined with LF, as the specification requires.
  // Comments and non-data fields (event:, id:) are consumed and discarded.
  // An event carrying no data is not dispatched.
  [[nodiscard]] bool next_event(std::string_view& out_data) noexcept;

  // How many events were discarded for not fitting. Anything but zero is a
  // reason to question kMaxEventBytes.
  [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }

  // Call at the end of the stream, in case the last line has no trailing
  // newline. The gateway always ends with \n\n, so normally this produces
  // nothing.
  void finish() noexcept;

 private:
  void take_line(std::string_view line) noexcept;
  void dispatch() noexcept;

  // The receive buffer, where a line is assembled, is kept separate from
  // the event buffer, where data lines are joined. With a single buffer, an
  // event could not be held while the next block arrived.
  std::array<char, kMaxEventBytes> line_{};
  std::size_t line_len_ = 0;
  bool line_overflow_ = false;
  bool last_was_cr_ = false;

  std::array<char, kMaxEventBytes> event_{};
  std::size_t event_len_ = 0;
  bool event_has_data_ = false;
  bool event_overflow_ = false;

  // Where completed events wait, packed as [2-byte length][body].
  //
  // Needed so that several events completing in one feed are not lost: a
  // single block can carry "data: a", a blank line, "data: b" and another
  // blank line. Once drained, the next feed rewinds to the start.
  std::array<char, kMaxEventBytes * 2> fifo_{};
  std::size_t fifo_write_ = 0;
  std::size_t fifo_read_ = 0;

  std::size_t dropped_ = 0;
};

}  // namespace stackchan::domain
