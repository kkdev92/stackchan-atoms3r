#include "stackchan/net/wifi.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace stackchan::net {
namespace {

constexpr char kTag[] = "net";

// Where credentials live.
constexpr char kNvsNamespace[] = "wifi";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPassword[] = "pass";

Status g_status{};
domain::NetworkPolicy g_policy{1000, 60000, 3};
bool g_started = false;
bool g_credentials_present = false;

// Networks found by the scan at startup. The setup page reads this. No
// further scan runs once the access point is up, so it does not change.
constexpr std::size_t kMaxScanEntries = 20;
std::array<ScanEntry, kMaxScanEntries> g_scan{};
std::size_t g_scan_count = 0;

// Signals that a scan has finished. Set from the event handler and waited
// for with a deadline.
std::atomic<bool> g_scan_done{false};

// Messages from the event task to the tick task.
//
// The policy object has no locking, so only tick touches it. Event handlers
// set a flag and the next tick passes it on.
std::atomic<bool> g_sta_got_ip{false};
std::atomic<bool> g_sta_lost{false};

// How many clients the access point has. Counted by events, reported to
// the policy by tick.
std::atomic<std::uint8_t> g_ap_guests{0};

// Generate the access point's passphrase: new each boot, and shown on the
// screen.
//
// The obvious random function is only a pseudo-random sequence until the
// hardware entropy source is running, so that is enabled explicitly first.
// **This must be called before Wi-Fi is initialised**, because the entropy
// source and the radio cannot both use the analogue front end.
void make_ap_password(char* out, std::size_t size) {
  static constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
  bootloader_random_enable();
  for (std::size_t i = 0; i + 1 < size; ++i) {
    out[i] = kAlphabet[esp_random() % 32];
  }
  bootloader_random_disable();
  out[size - 1] = 0;
}

// Build the access point name from the chip's MAC address, so that two
// units in the same room do not present the same SSID.
void build_ap_ssid(char* out, std::size_t size) {
  std::array<std::uint8_t, 6> mac{};
  if (esp_read_mac(mac.data(), ESP_MAC_WIFI_STA) != ESP_OK) {
    mac.fill(0);
  }
  const std::uint32_t id = (static_cast<std::uint32_t>(mac[2]) << 16) |
                           (static_cast<std::uint32_t>(mac[1]) << 8) |
                           static_cast<std::uint32_t>(mac[0]);
  // %X rather than a variable-width conversion, so the name is always the
  // same length.
  std::snprintf(out, size, "STACKCHAN-%X", static_cast<unsigned>(id));
}

// Read the credentials from NVS into the caller's buffers.
[[nodiscard]] bool load_credentials(char* ssid, std::size_t ssid_size, char* password,
                                    std::size_t password_size) {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }

  std::size_t length = ssid_size;
  const esp_err_t got_ssid = nvs_get_str(handle, kKeySsid, ssid, &length);
  length = password_size;
  const esp_err_t got_password = nvs_get_str(handle, kKeyPassword, password, &length);
  nvs_close(handle);

  // An empty SSID counts as unconfigured.
  return got_ssid == ESP_OK && got_password == ESP_OK && ssid[0] != 0;
}

void on_wifi_event(void*, esp_event_base_t base, std::int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
    g_scan_done.store(true, std::memory_order_release);
    return;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
    g_ap_guests.fetch_add(1, std::memory_order_acq_rel);
    ESP_LOGI(kTag, "a guest joined the access point");
    return;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
    // Never below zero: a miscount must not stop reconnection forever.
    std::uint8_t current = g_ap_guests.load(std::memory_order_acquire);
    while (current > 0 && !g_ap_guests.compare_exchange_weak(
                              current, static_cast<std::uint8_t>(current - 1),
                              std::memory_order_acq_rel)) {
    }
    ESP_LOGI(kTag, "a guest left the access point");
    return;
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // Disconnected, or never connected. Only a flag is set here; recording
    // the failure is tick's job.
    //
    // Forgetting to set it leaves the state stuck at connecting, and the
    // device never tries again — the driver does not reconnect by itself.
    //
    // The reason code is logged because it is genuinely diagnostic: a
    // handshake timeout almost always means a mistyped password, while "no
    // AP found" means out of range or the wrong SSID.
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(data);
    ESP_LOGW(kTag, "disconnected, reason=%d", static_cast<int>(event->reason));
    g_status.ip[0] = 0;
    g_sta_lost.store(true, std::memory_order_release);
    return;
  }
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    std::snprintf(g_status.ip, sizeof(g_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
    g_sta_got_ip.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "connected, ip=%s", g_status.ip);
  }
}

// Apply the access point configuration. This has to happen before the
// radio is started.
//
// Configuring it afterwards does not take effect: with no SSID set at
// start, the access point interface never comes up in a usable state, and
// nothing is broadcast at all.
[[nodiscard]] bool configure_access_point() {
  build_ap_ssid(g_status.ap_ssid, sizeof(g_status.ap_ssid));

  wifi_config_t config = {};
  const std::size_t length = std::strlen(g_status.ap_ssid);
  std::memcpy(config.ap.ssid, g_status.ap_ssid, length);
  config.ap.ssid_len = static_cast<std::uint8_t>(length);
  config.ap.channel = 1;
  config.ap.max_connection = 2;
  // WPA2 rather than an open network, because the setup page carries the
  // home Wi-Fi password. The key is shown on the display.
  config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  std::memcpy(config.ap.password, g_status.ap_password,
              std::strlen(g_status.ap_password));

  const esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &config);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "could not configure the access point: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

[[nodiscard]] bool run_bounded_scan();

// Raise the access point, so a device that has credentials but cannot
// connect can still be reconfigured.
//
// Apply this boot's access-point configuration after switching to combined
// mode. A boot that starts with credentials does not configure the access point
// until recovery mode is needed.
[[nodiscard]] bool start_access_point() {
  if (g_status.access_point_up) {
    return true;
  }

  // If the list is empty, scan before raising the access point.
  //
  // A boot with credentials skips the scan at startup, since connecting is
  // the likely outcome and a scan costs several seconds every time. Getting
  // here means it did not connect, so without this the setup page would
  // offer an empty list. Scanning is impossible once the access point is
  // up, so this is the last opportunity.
  //
  // Reaching here means the station side is backing off, so the few seconds
  // cost nothing, and no one is connected to the access point yet. If it
  // fails, carry on: the page also accepts a network typed by hand.
  if (g_scan_count == 0) {
    (void)run_bounded_scan();
  }

  const esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "could not switch to APSTA: %s", esp_err_to_name(err));
    return false;
  }
  if (!configure_access_point()) {
    return false;
  }
  g_status.access_point_up = true;
  // This layer does not log the passphrase. The UI shows it on-screen and uses
  // a serial fallback only when no display is available.
  ESP_LOGI(kTag, "access point up: %s (wpa2)", g_status.ap_ssid);
  return true;
}

void try_connect(std::uint32_t now_ms) {
  std::array<char, 33> ssid{};
  std::array<char, 65> password{};
  if (!load_credentials(ssid.data(), ssid.size(), password.data(), password.size())) {
    g_credentials_present = false;
    g_policy.set_provisioned(false);
    return;
  }

  wifi_config_t config = {};
  std::memcpy(config.sta.ssid, ssid.data(), std::strlen(ssid.data()));
  std::memcpy(config.sta.password, password.data(), std::strlen(password.data()));

  if (esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK) {
    ESP_LOGE(kTag, "could not configure the station");
    g_policy.on_failed(now_ms);
    return;
  }

  g_policy.on_attempt_started(now_ms);
  // The SSID is logged; the password is not.
  ESP_LOGI(kTag, "connecting to %s", ssid.data());
  if (esp_wifi_connect() != ESP_OK) {
    g_policy.on_failed(now_ms);
  }
}

// Run a scan. Only valid while the radio is up and the access point is not.
//
// Waits with a deadline rather than using the blocking form, which never
// returns in an environment with no radio response and would take the
// caller down with it. No unbounded waits (invariant 4).
[[nodiscard]] bool run_bounded_scan() {
  wifi_scan_config_t config = {};
  config.show_hidden = false;
  g_scan_done.store(false, std::memory_order_release);
  const esp_err_t err = esp_wifi_scan_start(&config, false);
  if (err != ESP_OK) {
    // Refused while a connection attempt is in progress. Give up without
    // disturbing anything.
    ESP_LOGW(kTag, "scan could not be started: %s", esp_err_to_name(err));
    return false;
  }

  constexpr int kScanDeadlineMs = 6000;
  int waited = 0;
  while (!g_scan_done.load(std::memory_order_acquire) && waited < kScanDeadlineMs) {
    vTaskDelay(pdMS_TO_TICKS(100));
    waited += 100;
  }
  if (!g_scan_done.load(std::memory_order_acquire)) {
    ESP_LOGW(kTag, "scan did not finish within %d ms; continuing without a list",
             kScanDeadlineMs);
    (void)esp_wifi_scan_stop();
    return false;
  }

  std::uint16_t count = 0;
  (void)esp_wifi_scan_get_ap_num(&count);
  std::array<wifi_ap_record_t, kMaxScanEntries> records{};
  std::uint16_t taken =
      count < kMaxScanEntries ? count : static_cast<std::uint16_t>(kMaxScanEntries);
  if (esp_wifi_scan_get_ap_records(&taken, records.data()) != ESP_OK) {
    return false;
  }
  g_scan_count = 0;
  for (std::uint16_t i = 0; i < taken; ++i) {
    const auto* ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (ssid[0] == 0) {
      continue;
    }
    std::snprintf(g_scan[g_scan_count].ssid, sizeof(g_scan[g_scan_count].ssid), "%s",
                  ssid);
    g_scan[g_scan_count].rssi = records[i].rssi;
    ++g_scan_count;
  }
  ESP_LOGI(kTag, "scanned %u network(s) before opening the access point",
           static_cast<unsigned>(g_scan_count));
  return true;
}

// For a boot with no credentials: scan the surroundings and keep the
// results before raising the access point.
//
// The radio is started in station mode alone, scanned, and stopped again.
// With no access point up, there are no probe responses to collide with.
// It costs a few seconds, once, on a boot that was going to need setting up
// anyway.
void scan_before_access_point() {
#ifdef STACKCHAN_QEMU
  // Scanning hangs under the emulator, which has no radio to answer. This
  // is a hardware-only feature, so continue there without a list.
  ESP_LOGW(kTag, "scan skipped in the emulator");
  return;
#endif
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
    ESP_LOGW(kTag, "scan skipped: sta could not be started");
    return;
  }

  if (!run_bounded_scan()) {
    ESP_LOGW(kTag, "scan before access point failed");
  }

  // Stop before returning, so the caller can apply the access point
  // configuration and start again — configuration has to precede start.
  (void)esp_wifi_stop();
}

}  // namespace

bool begin() {
  if (g_started) {
    return true;
  }

  // NVS holds the credentials, so bring it up first.
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Not in a usable state, so rebuild it. The credentials are lost,
    // which is better than continuing with storage that cannot be read.
    ESP_LOGW(kTag, "nvs needs to be erased: %s", esp_err_to_name(err));
    if (nvs_flash_erase() != ESP_OK) {
      return false;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_flash_init failed: %s", esp_err_to_name(err));
    return false;
  }

  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) {
    ESP_LOGE(kTag, "netif or event loop could not be started");
    return false;
  }

  // Generate the passphrase before the radio is initialised.
  make_ap_password(g_status.ap_password, sizeof(g_status.ap_password));

#ifdef STACKCHAN_QEMU
  // Starting the radio hangs under the emulator, which cannot complete RF
  // calibration. Finish booting without it: the HTTP server runs on the IP
  // stack and still comes up.
  ESP_LOGW(kTag, "wifi disabled in the emulator");
  build_ap_ssid(g_status.ap_ssid, sizeof(g_status.ap_ssid));
  g_started = true;
  return true;
#endif
  (void)esp_netif_create_default_wifi_sta();
  (void)esp_netif_create_default_wifi_ap();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_init failed");
    return false;
  }

  (void)esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event,
                                            nullptr, nullptr);
  (void)esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &on_wifi_event, nullptr, nullptr);

  std::array<char, 33> ssid{};
  std::array<char, 65> password{};
  g_credentials_present =
      load_credentials(ssid.data(), ssid.size(), password.data(), password.size());
  g_policy.set_provisioned(g_credentials_present);
  ESP_LOGI(kTag, "credentials %s", g_credentials_present ? "found" : "not stored");

  // With no credentials, the access point is needed immediately. Ask the
  // policy rather than deciding here.
  const bool need_ap = g_policy.should_open_access_point();

  // If it is needed, scan before raising it. Scanning afterwards takes
  // the driver down.
  if (need_ap) {
    scan_before_access_point();
  }

  // Station and access point run together, so the device can be
  // reconfigured while it is connected.
  //
  // Set mode, then access-point configuration, then start. Configuration is
  // accepted only after the mode includes an access point, and its SSID must
  // be set before the radio starts.
  const wifi_mode_t mode = need_ap ? WIFI_MODE_APSTA : WIFI_MODE_STA;
  if (esp_wifi_set_mode(mode) != ESP_OK) {
    ESP_LOGE(kTag, "wifi mode could not be set");
    return false;
  }
  if (need_ap && !configure_access_point()) {
    return false;
  }
  if (esp_wifi_start() != ESP_OK) {
    ESP_LOGE(kTag, "wifi could not be started");
    return false;
  }
  g_status.access_point_up = need_ap;
  if (need_ap) {
    // Not logged; see the note where the access point is first raised.
    ESP_LOGI(kTag, "access point up: %s (wpa2)", g_status.ap_ssid);
  }

  // Confirm it actually came up. Applying the configuration does not prove
  // anything; a running access point has an address on its interface.
  wifi_mode_t actual = WIFI_MODE_NULL;
  (void)esp_wifi_get_mode(&actual);
  esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_ip_info_t ap_ip = {};
  const bool got_ip =
      ap_netif != nullptr && esp_netif_get_ip_info(ap_netif, &ap_ip) == ESP_OK;
  ESP_LOGI(kTag, "mode=%d ap_netif=%s ap_ip=" IPSTR, static_cast<int>(actual),
           ap_netif != nullptr ? "yes" : "no",
           IP2STR(got_ip ? &ap_ip.ip : &ap_ip.ip));

  // Read the configuration back, to see whether what was written took.
  wifi_config_t readback = {};
  if (esp_wifi_get_config(WIFI_IF_AP, &readback) == ESP_OK) {
    ESP_LOGI(kTag, "ap config ssid=\"%s\" len=%u channel=%u authmode=%d hidden=%u",
             reinterpret_cast<const char*>(readback.ap.ssid),
             static_cast<unsigned>(readback.ap.ssid_len),
             static_cast<unsigned>(readback.ap.channel),
             static_cast<int>(readback.ap.authmode),
             static_cast<unsigned>(readback.ap.ssid_hidden));
  }

  g_started = true;
  return true;
}

void tick(std::uint32_t now_ms) {
  if (!g_started) {
    return;
  }
#ifdef STACKCHAN_QEMU
  (void)now_ms;
  return;  // no radio, so there is nothing to reconnect or raise
#endif

  // Hand the event flags to the policy here.
  //
  // Connected is examined first. In the other order, a connection that came
  // up and immediately dropped would lose the drop and be left reported as
  // connected.
  if (g_sta_got_ip.exchange(false, std::memory_order_acq_rel)) {
    g_policy.on_connected();
  }
  if (g_sta_lost.exchange(false, std::memory_order_acq_rel)) {
    g_policy.on_failed(now_ms);
  }
  g_policy.set_guest_count(g_ap_guests.load(std::memory_order_acquire));

  if (g_policy.should_attempt(now_ms)) {
    try_connect(now_ms);
  }

  if (g_policy.should_open_access_point() && !g_status.access_point_up) {
    (void)start_access_point();
  }

  g_status.phase = g_policy.phase();
  g_status.consecutive_failures = g_policy.consecutive_failures();
}

const Status& status() { return g_status; }

bool save_credentials(std::string_view ssid, std::string_view password) {
  if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
    return false;
  }

  // string_view is not null-terminated, so copy it.
  std::array<char, 33> ssid_text{};
  std::array<char, 65> password_text{};
  std::memcpy(ssid_text.data(), ssid.data(), ssid.size());
  std::memcpy(password_text.data(), password.data(), password.size());

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  const bool wrote = nvs_set_str(handle, kKeySsid, ssid_text.data()) == ESP_OK &&
                     nvs_set_str(handle, kKeyPassword, password_text.data()) == ESP_OK &&
                     nvs_commit(handle) == ESP_OK;
  nvs_close(handle);

  if (wrote) {
    g_credentials_present = true;
    // The next tick connects. Nothing waits here.
    g_policy.set_provisioned(true);
    ESP_LOGI(kTag, "credentials stored for %s", ssid_text.data());
  }
  return wrote;
}

bool has_credentials() { return g_credentials_present; }

std::size_t scan_results(const ScanEntry** out) {
  if (out == nullptr) {
    return 0;
  }
  *out = g_scan.data();
  return g_scan_count;
}

}  // namespace stackchan::net
