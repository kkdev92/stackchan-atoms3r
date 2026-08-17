#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// Decoding base64, which is how audio arrives inside a JSON event.
//
// Strict on purpose. An invalid character or a bad length is a failure, and
// nothing partially decoded is reported as success. Corrupt audio does not
// fail quietly — it comes out of the speaker as noise — so it is better to
// drop the event than to play whatever the bytes happened to mean.

namespace stackchan::domain {

// Decode into out, returning the number of bytes written.
//
// Returns SIZE_MAX on failure — an invalid character, a length that is not
// a multiple of four, misplaced padding, or too small a buffer — in which
// case out must not be used.
[[nodiscard]] std::size_t base64_decode(std::string_view text, std::uint8_t* out,
                                        std::size_t capacity) noexcept;

// How large the decoded data will be, counting the padding so the answer
// is exact. SIZE_MAX if the length could not be valid.
[[nodiscard]] std::size_t base64_decoded_size(std::string_view text) noexcept;

}  // namespace stackchan::domain
