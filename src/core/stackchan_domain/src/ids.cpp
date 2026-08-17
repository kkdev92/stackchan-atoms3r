#include "stackchan/domain/ids.hpp"

#include <array>

namespace stackchan::domain {
namespace {

// Crockford base32, which leaves out I, L, O and U so that 1 and I, or 0
// and O, cannot be confused by someone reading an identifier out of a log.
constexpr std::array<char, 32> kAlphabet = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'J', 'K', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'V', 'W', 'X', 'Y', 'Z'};
constexpr std::size_t kAlphabetSize = kAlphabet.size();

// 128 bits as 26 characters, five bits at a time from the top. 26 times
// five is 130, so the first character carries only three.
void encode(std::uint64_t high, std::uint64_t low,
            std::array<char, BootId::kTextLength>& out) noexcept {
  // Filled from the back, taking the low five bits and shifting right.
  for (std::size_t i = BootId::kTextLength; i > 0; --i) {
    out[i - 1] = kAlphabet[low % kAlphabetSize];
    // Shift the 128-bit value right by five, carrying what falls out of
    // the high word into the top of the low word.
    low = (low >> 5) | (high << 59);
    high >>= 5;
  }
}

// Unsigned decimal. std::to_chars is avoided because its integer overloads
// are not guaranteed to be present in this toolchain's library.
[[nodiscard]] std::size_t write_decimal(std::uint64_t value, char* out) noexcept {
  if (value == 0) {
    out[0] = '0';
    return 1;
  }
  std::array<char, 20> reversed{};
  std::size_t count = 0;
  while (value > 0) {
    reversed[count++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  }
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = reversed[count - 1 - i];
  }
  return count;
}

}  // namespace

BootId::BootId() noexcept { text_.fill('0'); }

BootId BootId::from_entropy(std::uint64_t high, std::uint64_t low) noexcept {
  BootId id;
  id.high_ = high;
  id.low_ = low;
  encode(high, low, id.text_);
  return id;
}

Id::Id(const BootId& boot, std::uint64_t sequence) noexcept : sequence_(sequence) {
  const std::string_view boot_text = boot.text();
  std::size_t at = 0;
  for (const char c : boot_text) {
    text_[at++] = c;
  }
  text_[at++] = '-';
  at += write_decimal(sequence, text_.data() + at);
  length_ = at;
}

}  // namespace stackchan::domain
