#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The AtomS3R's pin assignment, and how conflicts between uses are decided.
//
// Pin numbers are not scattered across drivers; this is the single source
// of truth. None of the reasoning touches hardware, so it can be tested on
// a PC.
//
// Where the numbers came from: docs/hardware/pins-and-peripherals.md, which
// records what was measured and cites the sources.

namespace stackchan::board {

// The LCD controller differs between units: some carry a GC9107 and some an
// ST7735, and both are in circulation. "AtomS3R" alone does not tell you
// which is on the board in front of you.
//
// They need different pixel offsets, so guessing wrong gives a picture
// shifted by a few pixels rather than an obvious failure. Never hard-code
// one of them. The display driver reads the panel ID at boot and selects the
// matching geometry. See docs/hardware/pins-and-peripherals.md.
enum class DisplayRevision : std::uint8_t {
  unknown,
  gc9107,
  st7735,
};

// What claims the six bottom-header pins. Attaching the Atomic Voice Base
// consumes all of them.
enum class BottomHeaderUsage : std::uint8_t {
  unused,
  voice_base,
  application,
};

// What G1 and G2 on the Grove connector (HY2.0-4P) are used for. The same
// two pins cannot drive a servo and carry I2C at once.
enum class GroveUsage : std::uint8_t {
  unused,
  i2c,
  servo_pwm,
  uart,
};

// The outcome of asking for a pin.
enum class PinClaimResult : std::uint8_t {
  ok,
  // Held by a device on the board itself. Applications must not touch it.
  reserved_by_board,
  // Another use already claimed it.
  already_claimed,
  // The pin is not brought out of the AtomS3R at all.
  not_exposed,
};

struct BoardProfile {
  DisplayRevision display_revision = DisplayRevision::unknown;
  BottomHeaderUsage bottom_header = BottomHeaderUsage::unused;
  GroveUsage grove = GroveUsage::unused;
};

// Pins brought out of the module.
inline constexpr std::array<int, 2> kGroveGpio{1, 2};
inline constexpr std::array<int, 6> kBottomGpio{5, 6, 7, 8, 38, 39};

// Already used inside the AtomS3R. Not available for general use.
//   G0, G45              internal I2C (LP5562, BMI270)
//   G47                  IR transmit
//   G41                  user button
//   G48, G42, G21, G15, G14   LCD
//   G19, G20             USB
inline constexpr std::array<int, 12> kBoardReservedGpio{0,  45, 47, 41, 48, 42,
                                                       21, 15, 14, 19, 20, 43};

// Whether the pin is brought out of the module.
[[nodiscard]] bool is_exposed_gpio(int gpio) noexcept;

// Whether a device on the board already holds the pin.
[[nodiscard]] bool is_board_reserved_gpio(int gpio) noexcept;

// Whether this configuration leaves the pin available.
//
// With the Voice Base attached, all six bottom pins belong to audio. If
// Grove is carrying I2C, those same two pins cannot also drive a servo.
[[nodiscard]] PinClaimResult claim_gpio(const BoardProfile& profile, int gpio,
                                        GroveUsage intent) noexcept;

// Whether the LCD revision has been established. Choosing a display driver
// while it is still unknown produces units with a broken picture.
[[nodiscard]] bool has_known_display(const BoardProfile& profile) noexcept;

// Returns one inconsistency in the configuration, or nullopt if there is
// none. Intended to be called at boot, to stop or warn.
[[nodiscard]] std::optional<std::string_view> validate(
    const BoardProfile& profile) noexcept;

}  // namespace stackchan::board
