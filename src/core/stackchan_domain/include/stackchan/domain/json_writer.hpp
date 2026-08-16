#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Writing JSON, straight into a buffer the caller supplies.
//
// Why this is written by hand
// ---------------------------
// The important property is what happens when the output does not fit. A
// library that quietly truncates gives you a document that looks fine and
// parses wrong, and the caller never learns. Here, running out of room sets
// overflowed(), everything after it is discarded, and valid() refuses the
// result. A half-written document is never reported as a success.
//
// Nothing is allocated. Building JSON is a frequent operation, and repeated
// allocation of short-lived buffers fragments a 320 KB heap that Wi-Fi and
// TLS also need contiguous space in.
//
// Reading is elsewhere
// --------------------
// This writes only. Parsing incoming requests happens on the platform side.
// Writing lives in core so that anything which assembles a structure — the
// capability list, for one — can be checked on a PC.

namespace stackchan::domain {

class JsonWriter {
 public:
  // How deep objects and arrays may nest before overflowed() is set. The
  // API's structures are around four deep, so this is not tight.
  static constexpr std::size_t kMaxDepth = 12;

  JsonWriter(char* buffer, std::size_t capacity) noexcept
      : buffer_(buffer), capacity_(capacity) {}

  void begin_object() noexcept;
  void end_object() noexcept;
  void begin_array() noexcept;
  void end_array() noexcept;

  // A key in an object. Exactly one value must follow.
  void key(std::string_view name) noexcept;

  void value(std::string_view text) noexcept;
  void value(bool flag) noexcept;
  void value(std::uint64_t number) noexcept;
  void value(std::int64_t number) noexcept;
  void null_value() noexcept;

  // Takes a C string explicitly.
  //
  // Without this overload, const char* binds to bool: pointer-to-bool is a
  // standard conversion and beats the user-defined one to string_view. The
  // result is a field that should read "connected" silently becoming true,
  // which is the kind of bug that survives review.
  void value(const char* text) noexcept { value(std::string_view{text}); }

  // Splice in a document that has already been built.
  //
  // When a response is assembled, the handler writes its payload first,
  // because whether the response is a success or an error depends on how
  // that went. The envelope is therefore written around a payload that
  // already exists.
  //
  // Nothing is escaped here, so the caller has to pass valid JSON. In
  // practice that means checking valid() on the writer that produced it.
  void raw_json(std::string_view json) noexcept;

  // Key and value together, to keep call sites short.
  void member(std::string_view name, std::string_view text) noexcept;
  void member(std::string_view name, bool flag) noexcept;
  void member(std::string_view name, std::uint64_t number) noexcept;
  void member(std::string_view name, std::int64_t number) noexcept;
  // Same reason as value(const char*): stop it binding to bool.
  void member(std::string_view name, const char* text) noexcept {
    member(name, std::string_view{text});
  }

  // Whether the output ran out of room. If true, text() is unusable.
  [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

  // Nothing overflowed and every open bracket was closed. Never send a
  // document for which this is false.
  [[nodiscard]] bool valid() const noexcept { return !overflowed_ && depth_ == 0; }

  [[nodiscard]] std::string_view text() const noexcept {
    return std::string_view{buffer_, length_};
  }

  [[nodiscard]] std::size_t length() const noexcept { return length_; }

  // How many bytes the document needed. Useful for sizing a buffer; after
  // an overflow it means "at least this many".
  [[nodiscard]] std::size_t required() const noexcept { return required_; }

  void reset() noexcept;

 private:
  void put(char c) noexcept;
  void put(std::string_view text) noexcept;
  void put_escaped(std::string_view text) noexcept;
  void separate() noexcept;
  void open(char bracket, bool is_object) noexcept;
  void close(char bracket, bool is_object) noexcept;

  char* buffer_;
  std::size_t capacity_;
  std::size_t length_ = 0;
  std::size_t required_ = 0;
  std::size_t depth_ = 0;
  bool overflowed_ = false;
  bool needs_comma_ = false;
  // Whether each open level is an object or an array, so that a mismatched
  // closing bracket can be caught.
  std::array<bool, kMaxDepth> is_object_{};
};

}  // namespace stackchan::domain
