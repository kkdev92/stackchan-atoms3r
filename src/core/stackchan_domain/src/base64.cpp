#include "stackchan/domain/base64.hpp"

namespace stackchan::domain {
namespace {

constexpr std::uint8_t kInvalid = 0xFF;

// Character to six-bit value, by lookup: faster than branching, and it
// validates at the same time.
[[nodiscard]] std::uint8_t value_of(char c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<std::uint8_t>(c - 'A');
  }
  if (c >= 'a' && c <= 'z') {
    return static_cast<std::uint8_t>(c - 'a' + 26);
  }
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0' + 52);
  }
  if (c == '+') {
    return 62;
  }
  if (c == '/') {
    return 63;
  }
  return kInvalid;
}

}  // namespace

std::size_t base64_decoded_size(std::string_view text) noexcept {
  if (text.empty()) {
    return 0;
  }
  if (text.size() % 4 != 0) {
    return SIZE_MAX;
  }
  std::size_t size = text.size() / 4 * 3;
  if (text.back() == '=') {
    --size;
    if (text[text.size() - 2] == '=') {
      --size;
    }
  }
  return size;
}

std::size_t base64_decode(std::string_view text, std::uint8_t* out,
                          std::size_t capacity) noexcept {
  const std::size_t expected = base64_decoded_size(text);
  if (expected == SIZE_MAX || expected > capacity || out == nullptr) {
    return text.empty() && out != nullptr ? 0 : SIZE_MAX;
  }
  if (text.empty()) {
    return 0;
  }

  std::size_t written = 0;
  for (std::size_t i = 0; i < text.size(); i += 4) {
    // Padding can only appear as the third or fourth character of the last
    // group.
    const bool last_group = (i + 4 == text.size());
    const char c0 = text[i];
    const char c1 = text[i + 1];
    const char c2 = text[i + 2];
    const char c3 = text[i + 3];

    const std::uint8_t v0 = value_of(c0);
    const std::uint8_t v1 = value_of(c1);
    if (v0 == kInvalid || v1 == kInvalid) {
      return SIZE_MAX;
    }

    out[written++] = static_cast<std::uint8_t>((v0 << 2) | (v1 >> 4));

    if (c2 == '=') {
      // Only "xx==" is valid. A '=' in the middle, or "xx=y", is not.
      if (!last_group || c3 != '=') {
        return SIZE_MAX;
      }
      break;
    }
    const std::uint8_t v2 = value_of(c2);
    if (v2 == kInvalid) {
      return SIZE_MAX;
    }
    out[written++] = static_cast<std::uint8_t>(((v1 & 0x0F) << 4) | (v2 >> 2));

    if (c3 == '=') {
      if (!last_group) {
        return SIZE_MAX;
      }
      break;
    }
    const std::uint8_t v3 = value_of(c3);
    if (v3 == kInvalid) {
      return SIZE_MAX;
    }
    out[written++] = static_cast<std::uint8_t>(((v2 & 0x03) << 6) | v3);
  }
  return written;
}

}  // namespace stackchan::domain
