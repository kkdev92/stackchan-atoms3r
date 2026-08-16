#include "stackchan/identity/device.hpp"

#include <array>
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"

namespace stackchan::identity {
namespace {

// Held statically, because DeviceIdentity keeps string_views into it.
std::array<char, 32> g_device_id{};
app::DeviceIdentity g_identity{};
// The executable's hash, in hex.
//
// Its length is fixed by configuration. ESP-IDF places a string of exactly
// that length in RAM at startup and the accessor merely copies it, so
// passing a larger buffer does not yield more characters.
std::array<char, CONFIG_APP_RETRIEVE_LEN_ELF_SHA + 1> g_build{};
bool g_collected = false;

// Two 64-bit random values.
//
// How random these are depends on circumstances. True randomness requires
// the hardware entropy source to be running, which during normal execution
// it is not: the bootloader enables it, seeds the generator and disables it
// again before the application starts. What the application sees is
// therefore a pseudo-random sequence whose seed differs each boot.
//
// That is sufficient here. boot_id has to distinguish one boot from
// another; it does not have to be unpredictable.
//
// **Do not derive anything secret from this.** The API token is generated
// separately, with the entropy source explicitly enabled — see
// identity/token.hpp.
[[nodiscard]] std::uint64_t random64() {
  return (static_cast<std::uint64_t>(esp_random()) << 32) | esp_random();
}

}  // namespace

const app::DeviceIdentity& collect() {
  if (g_collected) {
    return g_identity;
  }

  std::array<std::uint8_t, 6> mac{};
  // Specific to the board, and unchanged across restarts.
  if (esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
    mac.fill(0);
  }
  std::snprintf(g_device_id.data(), g_device_id.size(),
                "atoms3r-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
                mac[4], mac[5]);

  const esp_app_desc_t* app = esp_app_get_description();

  g_identity.device_id = std::string_view{g_device_id.data()};
  g_identity.boot_id = domain::BootId::from_entropy(random64(), random64());
  g_identity.firmware_version = app != nullptr ? app->version : "unknown";
  g_identity.firmware_idf = app != nullptr ? app->idf_ver : IDF_VER;

  // The executable's hash rather than a build timestamp. Reproducible
  // builds are enabled, which deliberately leaves the date and time empty:
  // the same source produces the same bytes, and a hash is what identity
  // means under those conditions.
  //
  // Nine characters is 36 bits, ample for telling which build is running on
  // one device. Raise the configured length if that ever stops being true.
  esp_app_get_elf_sha256(g_build.data(), g_build.size());
  g_identity.firmware_build = std::string_view{g_build.data()};

  g_collected = true;
  return g_identity;
}

std::uint64_t uptime_ms() {
  return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

}  // namespace stackchan::identity
