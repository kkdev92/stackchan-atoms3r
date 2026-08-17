#include "stackchan/domain/access_token.hpp"

namespace stackchan::domain {

AccessToken AccessToken::from_text(std::string_view text) noexcept {
  AccessToken token;
  if (text.size() != kLength) {
    // A value of the wrong length is rejected outright. Truncating or
    // padding it would let a short value appear to be accepted.
    return token;
  }
  for (std::size_t i = 0; i < kLength; ++i) {
    token.text_[i] = text[i];
  }
  token.set_ = true;
  return token;
}

bool AccessToken::matches(std::string_view presented) const noexcept {
  if (!set_) {
    // An unset token matches nothing. Were empty to match, a device that
    // failed to generate one would be left wide open.
    return false;
  }

  // No early return: the number of characters that matched would show up in
  // how long the rejection took, which is enough to guess a token one
  // character at a time. A length mismatch takes the same path.
  std::uint8_t difference = presented.size() == kLength ? 0 : 1;
  for (std::size_t i = 0; i < kLength; ++i) {
    const char expected = text_[i];
    // A short candidate still costs the same number of iterations; only the
    // read position wraps.
    const char actual = presented.empty() ? '\0' : presented[i % presented.size()];
    difference |= static_cast<std::uint8_t>(expected ^ actual);
  }
  return difference == 0;
}

}  // namespace stackchan::domain
