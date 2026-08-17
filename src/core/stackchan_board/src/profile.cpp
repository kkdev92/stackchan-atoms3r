#include "stackchan/board/profile.hpp"

#include <algorithm>

namespace stackchan::board {
namespace {

template <std::size_t N>
[[nodiscard]] bool contains(const std::array<int, N>& pins, int gpio) noexcept {
  return std::find(pins.begin(), pins.end(), gpio) != pins.end();
}

}  // namespace

bool is_exposed_gpio(int gpio) noexcept {
  return contains(kGroveGpio, gpio) || contains(kBottomGpio, gpio);
}

bool is_board_reserved_gpio(int gpio) noexcept {
  return contains(kBoardReservedGpio, gpio);
}

PinClaimResult claim_gpio(const BoardProfile& profile, int gpio,
                          GroveUsage intent) noexcept {
  if (is_board_reserved_gpio(gpio)) {
    return PinClaimResult::reserved_by_board;
  }

  if (!is_exposed_gpio(gpio)) {
    return PinClaimResult::not_exposed;
  }

  if (contains(kBottomGpio, gpio)) {
    // With the Voice Base attached, the six bottom pins are taken by its
    // control I2C and by I2S.
    if (profile.bottom_header == BottomHeaderUsage::voice_base) {
      return PinClaimResult::already_claimed;
    }

    return PinClaimResult::ok;
  }

  // From here on: G1 and G2 on the Grove connector.
  if (profile.grove == GroveUsage::unused) {
    return PinClaimResult::ok;
  }

  // Asking again for the same use is adding another device to the same bus,
  // so it is allowed. A different use would contend for the same two pins.
  return profile.grove == intent ? PinClaimResult::ok
                                 : PinClaimResult::already_claimed;
}

bool has_known_display(const BoardProfile& profile) noexcept {
  return profile.display_revision != DisplayRevision::unknown;
}

std::optional<std::string_view> validate(const BoardProfile& profile) noexcept {
  // Most of the exclusivity is expressed in the types, so there is nothing
  // to check for it here: BottomHeaderUsage holds one value, and so does
  // GroveUsage.
  //
  // What the types cannot prevent is proceeding while a value is still
  // undetermined.
  if (!has_known_display(profile)) {
    return "display revision is unknown; GC9107 and ST7735 units both exist";
  }

  return std::nullopt;
}

}  // namespace stackchan::board
