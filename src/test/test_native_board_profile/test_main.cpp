#include <unity.h>

#include "stackchan/board/profile.hpp"

using stackchan::board::BoardProfile;
using stackchan::board::BottomHeaderUsage;
using stackchan::board::claim_gpio;
using stackchan::board::DisplayRevision;
using stackchan::board::GroveUsage;
using stackchan::board::has_known_display;
using stackchan::board::is_board_reserved_gpio;
using stackchan::board::is_exposed_gpio;
using stackchan::board::PinClaimResult;
using stackchan::board::validate;

void setUp() {}
void tearDown() {}

namespace {

int claim(const BoardProfile& profile, int gpio, GroveUsage intent) {
  return static_cast<int>(claim_gpio(profile, gpio, intent));
}

constexpr int kOk = static_cast<int>(PinClaimResult::ok);
constexpr int kReserved = static_cast<int>(PinClaimResult::reserved_by_board);
constexpr int kClaimed = static_cast<int>(PinClaimResult::already_claimed);
constexpr int kNotExposed = static_cast<int>(PinClaimResult::not_exposed);

}  // namespace

// --- which pins are brought out ---

void test_grove_pins_are_exposed() {
  TEST_ASSERT_TRUE(is_exposed_gpio(1));
  TEST_ASSERT_TRUE(is_exposed_gpio(2));
}

void test_bottom_pins_are_exposed() {
  TEST_ASSERT_TRUE(is_exposed_gpio(5));
  TEST_ASSERT_TRUE(is_exposed_gpio(39));
}

void test_internal_pins_are_not_exposed() {
  // The display's SPI. Touching it breaks the picture.
  TEST_ASSERT_FALSE(is_exposed_gpio(21));
  TEST_ASSERT_TRUE(is_board_reserved_gpio(21));
}

void test_unknown_pin_is_rejected() {
  const BoardProfile profile{};
  TEST_ASSERT_EQUAL_INT(kNotExposed, claim(profile, 99, GroveUsage::i2c));
}

// --- pins held by devices on the board ---

void test_internal_i2c_is_reserved() {
  const BoardProfile profile{};
  // The internal I2C bus, shared by the LED driver and the motion sensor.
  TEST_ASSERT_EQUAL_INT(kReserved, claim(profile, 0, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kReserved, claim(profile, 45, GroveUsage::i2c));
}

void test_button_and_ir_are_reserved() {
  const BoardProfile profile{};
  TEST_ASSERT_EQUAL_INT(kReserved, claim(profile, 41, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kReserved, claim(profile, 47, GroveUsage::i2c));
}

// --- the Voice Base fills the bottom header ---

void test_bottom_pins_are_free_without_voice_base() {
  const BoardProfile profile{DisplayRevision::st7735, BottomHeaderUsage::unused,
                             GroveUsage::unused};
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 5, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 38, GroveUsage::i2c));
}

void test_voice_base_takes_every_bottom_pin() {
  // Four I2S lines plus two for control use up all six.
  const BoardProfile profile{DisplayRevision::st7735,
                             BottomHeaderUsage::voice_base, GroveUsage::unused};

  for (const int gpio : stackchan::board::kBottomGpio) {
    TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, gpio, GroveUsage::i2c));
  }
}

void test_grove_stays_free_with_voice_base() {
  // The Grove pins remain even with the Voice Base attached, which is where
  // an extension goes.
  const BoardProfile profile{DisplayRevision::st7735,
                             BottomHeaderUsage::voice_base, GroveUsage::unused};
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 1, GroveUsage::servo_pwm));
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 2, GroveUsage::servo_pwm));
}

// --- Grove serves one purpose at a time ---

void test_servo_and_i2c_cannot_share_grove() {
  // The same two pins cannot drive a servo and carry I2C at once.
  const BoardProfile profile{DisplayRevision::st7735, BottomHeaderUsage::unused,
                             GroveUsage::servo_pwm};

  TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, 1, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, 2, GroveUsage::i2c));
}

void test_same_usage_can_share_grove() {
  // I2C does allow several devices on one bus.
  const BoardProfile profile{DisplayRevision::st7735, BottomHeaderUsage::unused,
                             GroveUsage::i2c};

  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 1, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 2, GroveUsage::i2c));
}

void test_uart_conflicts_with_servo() {
  const BoardProfile profile{DisplayRevision::st7735, BottomHeaderUsage::unused,
                             GroveUsage::uart};
  TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, 1, GroveUsage::servo_pwm));
}

// --- establishing the display revision ---

void test_display_revision_must_be_decided() {
  const BoardProfile unknown{};
  TEST_ASSERT_FALSE(has_known_display(unknown));
  TEST_ASSERT_TRUE(validate(unknown).has_value());
}

void test_known_display_passes_validation() {
  const BoardProfile gc9107{DisplayRevision::gc9107, BottomHeaderUsage::unused,
                            GroveUsage::unused};
  TEST_ASSERT_TRUE(has_known_display(gc9107));
  TEST_ASSERT_FALSE(validate(gc9107).has_value());

  const BoardProfile st7735{DisplayRevision::st7735, BottomHeaderUsage::unused,
                            GroveUsage::unused};
  TEST_ASSERT_FALSE(validate(st7735).has_value());
}

// --- a configuration someone would actually build ---

void test_stackchan_configuration() {
  // The intended configuration: a Voice Base, and two servos on Grove.
  const BoardProfile profile{DisplayRevision::st7735,
                             BottomHeaderUsage::voice_base,
                             GroveUsage::servo_pwm};

  TEST_ASSERT_FALSE(validate(profile).has_value());

  // The servos run on the Grove pins.
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 1, GroveUsage::servo_pwm));
  TEST_ASSERT_EQUAL_INT(kOk, claim(profile, 2, GroveUsage::servo_pwm));

  // Which leaves no room for an I2C sensor in this configuration.
  TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, 1, GroveUsage::i2c));
  TEST_ASSERT_EQUAL_INT(kClaimed, claim(profile, 38, GroveUsage::i2c));
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_grove_pins_are_exposed);
  RUN_TEST(test_bottom_pins_are_exposed);
  RUN_TEST(test_internal_pins_are_not_exposed);
  RUN_TEST(test_unknown_pin_is_rejected);

  RUN_TEST(test_internal_i2c_is_reserved);
  RUN_TEST(test_button_and_ir_are_reserved);

  RUN_TEST(test_bottom_pins_are_free_without_voice_base);
  RUN_TEST(test_voice_base_takes_every_bottom_pin);
  RUN_TEST(test_grove_stays_free_with_voice_base);

  RUN_TEST(test_servo_and_i2c_cannot_share_grove);
  RUN_TEST(test_same_usage_can_share_grove);
  RUN_TEST(test_uart_conflicts_with_servo);

  RUN_TEST(test_display_revision_must_be_decided);
  RUN_TEST(test_known_display_passes_validation);

  RUN_TEST(test_stackchan_configuration);

  return UNITY_END();
}
