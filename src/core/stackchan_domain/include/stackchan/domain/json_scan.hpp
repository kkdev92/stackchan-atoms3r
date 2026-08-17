#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Reading the envelope JSON, by scanning rather than parsing.
//
// Why reading happens in core at all
// ----------------------------------
// So that interpreting a conversation stream — bytes to events to envelopes
// to audio and text — can be tested end to end on a PC. A parser that only
// exists on the device would put the most breakable part of the pipeline
// out of reach of the tests.
//
// This is not a general JSON parser. It reads the envelopes defined in
// docs/api/device-interface.md, and only needs to:
//
//   - find a key at one level of an object, without descending into nested
//     ones
//   - extract strings, integers, booleans and nested objects
//   - unescape strings, supporting only the escapes the contract permits.
//     A \uXXXX escape is a contract violation and is reported as a failure
//     rather than guessed at
//
// Malformed input returns false. It never crashes, and never hands back a
// value it only half read.

namespace stackchan::domain {

// Return a string value **still escaped**, as it appears between the
// quotes. Large values such as base64 audio can then be handled without
// copying them first.
[[nodiscard]] bool json_find_raw_string(std::string_view object, std::string_view key,
                                        std::string_view& out_raw) noexcept;

// Return a nested object, braces included. Used to lift out a payload.
[[nodiscard]] bool json_find_object(std::string_view object, std::string_view key,
                                    std::string_view& out_object) noexcept;

enum class JsonMemberResult : std::uint8_t { found, missing, invalid };

// Find an object-valued member while validating the complete outer object.
// Only a valid object in which the key is absent returns missing. A wrong
// value type, duplicate key, malformed member, or trailing data is invalid.
[[nodiscard]] JsonMemberResult json_find_object_checked(
    std::string_view object, std::string_view key,
    std::string_view& out_object) noexcept;

[[nodiscard]] bool json_find_integer(std::string_view object, std::string_view key,
                                     std::int64_t& out) noexcept;

[[nodiscard]] bool json_find_bool(std::string_view object, std::string_view key,
                                  bool& out) noexcept;

// Unescape a raw string value into out, returning the length written.
// SIZE_MAX on failure: too small a buffer, an unsupported escape, or input
// that ends mid-string.
[[nodiscard]] std::size_t json_unescape(std::string_view raw, char* out,
                                        std::size_t capacity) noexcept;

}  // namespace stackchan::domain
