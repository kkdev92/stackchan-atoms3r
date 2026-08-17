#include "stackchan/probe/hardware.hpp"

#include <array>
#include <cstdio>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"

namespace stackchan::probe {
namespace {

constexpr char kTag[] = "probe";

constexpr int kInternalScl = 0;
constexpr int kInternalSda = 45;
constexpr int kExternalScl = 39;
constexpr int kExternalSda = 38;

// The motion sensor's I2C address, which depends on one of its pins.
constexpr std::array<std::uint8_t, 2> kMotionSensorAddresses = {0x68, 0x69};

// The range scanned. The reserved addresses at each end are left out.
constexpr std::uint8_t kFirstAddress = 0x08;
constexpr std::uint8_t kLastAddress = 0x77;

// How long to wait for an answer. Short, because waiting on an address
// where nothing lives is pure delay.
constexpr int kProbeTimeoutMs = 20;

// One controller is used to examine both physical buses in turn.
//
// The chip has only two I2C controllers, and the second belongs to the
// display driver for backlight control. Claiming it here collides with
// that.
//
// The bus is closed after each scan, so one controller suffices. detect()
// must run before the display is initialised, or two controllers end up on
// the same pins.
constexpr i2c_port_num_t kScanPort = I2C_NUM_0;

Hardware g_hardware{};
bool g_detected = false;

struct BusScan {
  std::uint8_t count = 0;
  std::array<std::uint8_t, 16> found{};
  bool opened = false;
  // Why the bus could not be opened, so that "nothing there" and "could
  // not look" stay distinguishable.
  esp_err_t open_error = ESP_OK;
};

// The scan results, kept so that every address can be logged.
BusScan g_internal_scan{};
BusScan g_external_scan{};

// Read one register, to identify a chip.
//
// An address answering does not say what is at it. The audio codec, for
// instance, holds known values in two version registers, so reading them
// settles the question.
[[nodiscard]] bool read_register(i2c_master_bus_handle_t bus, std::uint8_t address,
                                 std::uint8_t reg, std::uint8_t& out) {
  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = address;
  device_config.scl_speed_hz = 100000;

  i2c_master_dev_handle_t device = nullptr;
  if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) {
    return false;
  }
  const esp_err_t err =
      i2c_master_transmit_receive(device, &reg, 1, &out, 1, kProbeTimeoutMs);
  (void)i2c_master_bus_rm_device(device);
  return err == ESP_OK;
}

// Scan one bus, returning the addresses that answered.
BusScan scan_bus(int scl, int sda) {
  BusScan result;

  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = kScanPort;
  bus_config.scl_io_num = static_cast<gpio_num_t>(scl);
  bus_config.sda_io_num = static_cast<gpio_num_t>(sda);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  const esp_err_t opened = i2c_new_master_bus(&bus_config, &bus);
  if (opened != ESP_OK) {
    result.open_error = opened;
    ESP_LOGW(kTag, "i2c bus scl=%d sda=%d could not be opened: %s", scl, sda,
             esp_err_to_name(opened));
    return result;
  }
  result.opened = true;

  for (std::uint8_t address = kFirstAddress; address <= kLastAddress; ++address) {
    // Only a write is attempted. An acknowledgement means something is
    // there; no data is sent.
    if (i2c_master_probe(bus, address, kProbeTimeoutMs) == ESP_OK) {
      if (result.count < result.found.size()) {
        result.found[result.count] = address;
      }
      ++result.count;
    }
  }

  // Release the bus once scanned. The real driver reopens it later.
  (void)i2c_del_master_bus(bus);
  return result;
}

// Identify the motion sensor on the internal bus, reopening the bus
// separately from the scan.
//
// It holds its chip ID in the first register, and the value is readable
// even in the low-power state it wakes up in.
void identify_internal() {
  if (!g_hardware.motion_sensor) {
    return;
  }

  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = kScanPort;
  bus_config.scl_io_num = static_cast<gpio_num_t>(kInternalScl);
  bus_config.sda_io_num = static_cast<gpio_num_t>(kInternalSda);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) {
    return;
  }

  std::uint8_t chip_id = 0;
  const bool got =
      read_register(bus, g_hardware.motion_sensor_address, 0x00, chip_id);
  if (got && chip_id == 0x24) {
    g_hardware.motion_sensor_identified = true;
    ESP_LOGI(kTag, "0x%02x is BMI270 (chip id %02x)",
             static_cast<unsigned>(g_hardware.motion_sensor_address),
             static_cast<unsigned>(chip_id));
  } else {
    // Do not assume. Report whatever was read.
    ESP_LOGW(kTag, "0x%02x not identified as BMI270 (reg 00=%02x, read %s)",
             static_cast<unsigned>(g_hardware.motion_sensor_address),
             static_cast<unsigned>(chip_id), got ? "ok" : "failed");
  }

  (void)i2c_del_master_bus(bus);
}

// Identify what answered on the external bus, reopening it separately.
void identify_external(const BusScan& scan) {
  if (!scan.opened || scan.count == 0) {
    return;
  }

  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = kScanPort;
  bus_config.scl_io_num = static_cast<gpio_num_t>(kExternalScl);
  bus_config.sda_io_num = static_cast<gpio_num_t>(kExternalSda);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) {
    return;
  }

  const std::uint8_t shown =
      scan.count < scan.found.size() ? scan.count : static_cast<std::uint8_t>(scan.found.size());
  for (std::uint8_t i = 0; i < shown; ++i) {
    const std::uint8_t address = scan.found[i];
    // The codec's version registers identify it.
    std::uint8_t id1 = 0;
    std::uint8_t id2 = 0;
    const bool got = read_register(bus, address, 0xFD, id1) &&
                     read_register(bus, address, 0xFE, id2);
    if (got && id1 == 0x83 && id2 == 0x11) {
      ESP_LOGI(kTag, "0x%02x is ES8311 (id %02x%02x)", static_cast<unsigned>(address),
               static_cast<unsigned>(id1), static_cast<unsigned>(id2));
      g_hardware.codec_identified = true;
      continue;
    }
    // Anything unidentified is reported by its raw values rather than
    // guessed at.
    ESP_LOGW(kTag, "0x%02x unidentified (reg fd=%02x fe=%02x, read %s)",
             static_cast<unsigned>(address), static_cast<unsigned>(id1),
             static_cast<unsigned>(id2), got ? "ok" : "failed");
  }

  (void)i2c_del_master_bus(bus);
}

}  // namespace

const Hardware& detect() {
  if (g_detected) {
    return g_hardware;
  }

  const BusScan internal = scan_bus(kInternalScl, kInternalSda);
  g_internal_scan = internal;
  g_hardware.internal_i2c_responded = internal.count > 0;
  g_hardware.internal_device_count = internal.count;

  for (std::uint8_t i = 0; i < internal.count && i < internal.found.size(); ++i) {
    for (const std::uint8_t candidate : kMotionSensorAddresses) {
      if (internal.found[i] == candidate) {
        g_hardware.motion_sensor = true;
        g_hardware.motion_sensor_address = candidate;
      }
    }
  }

  identify_internal();

  const BusScan external = scan_bus(kExternalScl, kExternalSda);
  g_external_scan = external;
  identify_external(external);
  g_hardware.external_device_count = external.count;
  g_hardware.external_bus_checked = external.opened;
  // Anything on the external bus means a Voice Base. Attaching one fills
  // all six bottom pins, so no other combination is possible.
  if (external.count > 0) {
    g_hardware.voice_base = true;
    g_hardware.voice_base_address = external.found[0];
  }

  g_detected = true;
  return g_hardware;
}

namespace {

// List the addresses found on one line, so that a person can work out
// which base is attached — not every configuration can be identified
// automatically.
void log_addresses(const char* label, const BusScan& scan) {
  if (scan.count == 0) {
    ESP_LOGI(kTag, "%s: no devices", label);
    return;
  }
  // Only sixteen are recorded; beyond that, report the count alone.
  char list[80] = {};
  int at = 0;
  const std::uint8_t shown =
      scan.count < scan.found.size() ? scan.count : static_cast<std::uint8_t>(scan.found.size());
  for (std::uint8_t i = 0; i < shown && at < static_cast<int>(sizeof(list)) - 6; ++i) {
    at += std::snprintf(list + at, sizeof(list) - static_cast<std::size_t>(at), "0x%02x ",
                        static_cast<unsigned>(scan.found[i]));
  }
  ESP_LOGI(kTag, "%s: %u device(s): %s", label, static_cast<unsigned>(scan.count), list);
}

}  // namespace

void log_scan() {
  const Hardware& hardware = detect();
  log_addresses("internal i2c", g_internal_scan);
  log_addresses("external i2c", g_external_scan);
  ESP_LOGI(kTag, "internal i2c: %u device(s)%s",
           static_cast<unsigned>(hardware.internal_device_count),
           hardware.motion_sensor ? "" : " (no motion sensor)");
  if (hardware.motion_sensor) {
    ESP_LOGI(kTag, "motion sensor at 0x%02x",
             static_cast<unsigned>(hardware.motion_sensor_address));
  }
  if (!hardware.external_bus_checked) {
    // This must not be read as "absent": it was never examined.
    ESP_LOGW(kTag, "external i2c: NOT CHECKED (bus unavailable)");
  } else {
    ESP_LOGI(kTag, "external i2c: %u device(s)%s",
             static_cast<unsigned>(hardware.external_device_count),
             hardware.voice_base ? "" : " (no voice base)");
  }
  if (hardware.voice_base) {
    ESP_LOGI(kTag, "voice base codec at 0x%02x",
             static_cast<unsigned>(hardware.voice_base_address));
  }
}

}  // namespace stackchan::probe
