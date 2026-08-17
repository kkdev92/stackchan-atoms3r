#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// The shared secret that authorises API requests.
//
// Why there is one
// ----------------
// Without it, anything on the same network can drive the device. That is a
// low bar on a home LAN, and it also forces whatever is talking to the
// device to identify it by source address — which becomes unsafe the moment
// a language model is choosing the arguments.
//
// With a token, the device a request controls is fixed by the credential
// rather than by anything a model can be persuaded to write.
//
// How it is compared
// ------------------
// Every byte is examined before answering, and the length is folded in the
// same way. Returning as soon as a mismatch is found would leak how much of
// a guess was correct, in the time taken to reject it.
//
// Where it comes from
// -------------------
// Not from here: this type only holds a value. Generating it is the
// platform's job, and doing that well is harder than it looks — the obvious
// random function on this chip is not seeded until the radio is running.

namespace stackchan::domain {

class AccessToken {
 public:
  // 32 characters, which is 160 bits in Crockford base32: far beyond
  // guessing, and still short enough to copy by hand.
  static constexpr std::size_t kLength = 32;

  // Unset. Matches nothing at all.
  [[nodiscard]] static AccessToken unset() noexcept { return AccessToken{}; }

  // Build from a string. A string of the wrong length leaves it unset.
  [[nodiscard]] static AccessToken from_text(std::string_view text) noexcept;

  [[nodiscard]] bool is_set() const noexcept { return set_; }

  [[nodiscard]] std::string_view text() const noexcept {
    return std::string_view{text_.data(), kLength};
  }

  // Whether a presented value matches.
  //
  // An unset token matches nothing, so a device that failed to generate one
  // refuses everything instead of accepting an empty string.
  [[nodiscard]] bool matches(std::string_view presented) const noexcept;

 private:
  AccessToken() noexcept { text_.fill('0'); }

  std::array<char, kLength> text_{};
  bool set_ = false;
};

}  // namespace stackchan::domain
