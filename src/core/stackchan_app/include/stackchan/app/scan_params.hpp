#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/ports/param_reader.hpp"

// A ParamReader backed by the scanner in domain, rather than a JSON
// library.
//
// Why not a library
// -----------------
// A parser that only exists on the device would leave the failure paths —
// malformed bodies, missing fields, values of the wrong type — testable
// only by sending real requests to real hardware. Scanning here means the
// whole route from body to response lives in core, so RequestRouter can be
// tested with the body included.
//
// What is given up is a library's tolerance: escapes the contract does not
// allow, duplicate keys. That is acceptable because the contract defines
// what is sent, and input outside it is treated as unreadable rather than
// guessed at.
//
// Where strings are put
// ---------------------
// The scanner returns strings still escaped, so unescaping needs somewhere
// to write. Each read takes a slice of an internal arena. If the arena runs
// out, the read fails rather than returning a half-unescaped value. Its
// size comfortably holds every argument the current commands accept, all at
// once.

namespace stackchan::app {

class ScanParams final : public ports::ParamReader {
 public:
  static constexpr std::size_t kArenaBytes = 512;

  // Points at the payload object, braces included. Not owned. Commands
  // without a payload get an empty view.
  explicit ScanParams(std::string_view payload_object) noexcept
      : payload_(payload_object) {}

  [[nodiscard]] bool read_string(std::string_view key,
                                 std::string_view& out) const override;
  [[nodiscard]] bool read_integer(std::string_view key, std::int64_t& out) const override;
  [[nodiscard]] bool read_bool(std::string_view key, bool& out) const override;
  [[nodiscard]] bool empty() const override;

 private:
  std::string_view payload_;

  // ParamReader's reads are const, so only the arena is mutable. Nothing
  // about the meaning of a read changes.
  mutable std::array<char, kArenaBytes> arena_{};
  mutable std::size_t arena_used_ = 0;
};

}  // namespace stackchan::app
