#include "stackchan/identity/token.hpp"

#include <array>
#include <cstring>

#include "bootloader_random.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace stackchan::identity {
namespace {

constexpr char kTag[] = "token";

// Stored beside the credentials, so that clearing the configuration
// clears the token too.
constexpr char kNvsNamespace[] = "wifi";
constexpr char kKeyToken[] = "api_token";

// Crockford base32: people read and retype this token, so the characters
// that are easily confused are left out.
constexpr std::array<char, 32> kAlphabet = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'J', 'K', 'M', 'N', 'P', 'Q', 'R', 'S', 'T', 'V', 'W', 'X', 'Y', 'Z'};

domain::AccessToken g_token = domain::AccessToken::unset();
bool g_loaded = false;
bool g_new = false;

// Draw 32 characters. Says nothing about where the randomness comes from —
// that is the caller's problem, and the two callers solve it differently.
void draw(std::array<char, domain::AccessToken::kLength>& out) {
  for (std::size_t i = 0; i < out.size(); ++i) {
    // The alphabet is a power of two, so the modulo introduces no bias.
    out[i] = kAlphabet[esp_random() % kAlphabet.size()];
  }
}

// Enable the hardware entropy source while generating before radio startup.
void generate_before_radio(std::array<char, domain::AccessToken::kLength>& out) {
  // Start mixing in noise from the analogue front end.
  bootloader_random_enable();

  draw(out);

  // Must be turned off again before the ADC, Wi-Fi or Bluetooth are used;
  // leaving it on stops the radio from starting.
  bootloader_random_disable();
}

// Write the token to NVS. Returns whether it is now stored.
[[nodiscard]] bool store(const char* text) {
  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return false;
  }
  const bool wrote =
      nvs_set_str(handle, kKeyToken, text) == ESP_OK && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return wrote;
}

}  // namespace

const domain::AccessToken& access_token() {
  if (g_loaded) {
    return g_token;
  }
  g_loaded = true;

  // NVS may not be up yet. Initialising it here is harmless if it is.
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (nvs_flash_erase() == ESP_OK) {
      err = nvs_flash_init();
    }
  }
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs unavailable: %s", esp_err_to_name(err));
    return g_token;
  }

  std::array<char, domain::AccessToken::kLength + 1> text{};

  nvs_handle_t handle = 0;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) == ESP_OK) {
    std::size_t length = text.size();
    const bool got = nvs_get_str(handle, kKeyToken, text.data(), &length) == ESP_OK;
    nvs_close(handle);
    if (got) {
      g_token = domain::AccessToken::from_text(std::string_view{text.data()});
      if (g_token.is_set()) {
        return g_token;
      }
      // A stored value of the wrong length. Generate a new one.
      ESP_LOGW(kTag, "stored token was unusable, making a new one");
    }
  }

  std::array<char, domain::AccessToken::kLength> fresh{};
  generate_before_radio(fresh);
  std::memcpy(text.data(), fresh.data(), fresh.size());
  text[fresh.size()] = 0;

  if (!store(text.data())) {
    // Unable to store it, so it would change on every restart. Warn,
    // because that makes it unusable.
    ESP_LOGE(kTag, "could not store the token; it will change on reboot");
  }

  g_token = domain::AccessToken::from_text(std::string_view{text.data()});
  g_new = true;
  return g_token;
}

bool token_is_new() { return g_new; }

bool rotate_access_token() {
  // Make sure the old one has been loaded, so that a failure below leaves
  // something valid behind rather than an unset token that refuses
  // everything.
  if (!access_token().is_set()) {
    return false;
  }

  std::array<char, domain::AccessToken::kLength + 1> text{};
  std::array<char, domain::AccessToken::kLength> fresh{};

  // No bootloader_random_enable() here: the radio is up, which both makes
  // that call unsafe and makes it unnecessary. The header explains why.
  draw(fresh);
  std::memcpy(text.data(), fresh.data(), fresh.size());
  text[fresh.size()] = 0;

  const domain::AccessToken candidate =
      domain::AccessToken::from_text(std::string_view{text.data()});
  if (!candidate.is_set()) {
    return false;
  }

  // Store before swapping so a storage failure leaves the current token
  // unchanged across restarts.
  if (!store(text.data())) {
    ESP_LOGE(kTag, "could not store the new token; keeping the old one");
    return false;
  }

  // Everything that checks a token holds a reference to this object, so the
  // change takes effect on the next request rather than the next restart.
  //
  // One reader lives on another task: the conversation client copies the
  // token when it posts to the gateway. Rotating during those few
  // microseconds would send a mixed value and that one conversation would
  // be rejected by a gateway that checks it. The command refuses to run
  // while a conversation is in progress, which leaves only the instant one
  // is starting, and the cost there is a single failed request rather than
  // anything lasting.
  g_token = candidate;

  ESP_LOGW(kTag, "access token rotated: %.*s",
           static_cast<int>(g_token.text().size()), g_token.text().data());
  ESP_LOGW(kTag, "previous token invalidated; store the new token securely");
  return true;
}

}  // namespace stackchan::identity
