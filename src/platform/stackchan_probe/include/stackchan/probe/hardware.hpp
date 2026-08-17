#pragma once

#include <cstdint>

// Find out what is actually attached.
//
// Why this exists
// ---------------
// The capabilities the device advertises have to reflect this unit. The
// board on its own has no microphone, speaker or servo; those arrive on an
// add-on base. What works therefore depends on what is plugged in.
//
// Assume they are present and a unit without them accepts recording
// requests and fails them. Assume they are absent and attaching one changes
// nothing.
//
// How it is checked
// -----------------
// By whether a device acknowledges on the I2C bus. Only an address is
// probed, with no data sent, so nothing happens if nobody is there.
//
//   internal bus   G0=SCL,  G45=SDA   the motion sensor and the backlight
//   external bus   G39=SCL, G38=SDA   the Voice Base codec's control lines
//
// A servo is driven by PWM and answers nothing, so it cannot be found this
// way. Anything undetectable is reported as unknown rather than absent,
// which is more useful than a confident wrong answer.

namespace stackchan::probe {

struct Hardware {
  // Whether anything answered on the internal bus. Nothing at all suggests
  // a wiring or power fault.
  bool internal_i2c_responded = false;

  // The motion sensor, at either of two addresses depending on a pin.
  bool motion_sensor = false;
  std::uint8_t motion_sensor_address = 0;

  // Whether its chip ID register returned the expected value.
  //
  // An address answering does not say what is at it, so the device is
  // identified properly, to the same standard as the audio codec.
  bool motion_sensor_identified = false;

  // Whether the external bus could be examined at all.
  //
  // False means "unknown", not "no Voice Base": if the bus could not be
  // opened, no scan happened. Conflating the two produces a unit that has
  // the hardware attached and refuses to use it.
  bool external_bus_checked = false;

  // The Voice Base codec. Meaningful only when the bus was examined.
  bool voice_base = false;
  std::uint8_t voice_base_address = 0;

  // Whether reading its version registers confirmed the codec.
  //
  // An address answering does not say what is at it. This is only true when
  // the device was actually read, and audio depends on it.
  bool codec_identified = false;

  // How many addresses answered, for checking the wiring.
  std::uint8_t internal_device_count = 0;
  std::uint8_t external_device_count = 0;
};

  // Probe once at boot. The result is retained.
[[nodiscard]] const Hardware& detect();

  // Log every address that answered, for when the wiring is in doubt.
void log_scan();

}  // namespace stackchan::probe
