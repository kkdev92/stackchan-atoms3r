#include "stackchan/input/button.hpp"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

namespace stackchan::input {
namespace {

// The user button, active low. The authoritative pin assignment is in
// stackchan_board/profile.hpp.
constexpr gpio_num_t kButtonPin = GPIO_NUM_41;

domain::ButtonDebouncer g_debouncer;
domain::ButtonGesture g_gestures;
bool g_ready = false;

}  // namespace

bool begin() {
  gpio_config_t config = {};
  config.pin_bit_mask = 1ULL << static_cast<std::uint32_t>(kButtonPin);
  config.mode = GPIO_MODE_INPUT;
  config.pull_up_en = GPIO_PULLUP_ENABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&config) != ESP_OK) {
    ESP_LOGE("input", "button gpio could not be configured");
    return false;
  }
  g_ready = true;
  return true;
}

domain::Gesture poll(std::uint32_t now_ms) {
  if (!g_ready) {
    return domain::Gesture{};
  }
  const bool pressed = gpio_get_level(kButtonPin) == 0;  // active low
  const domain::ButtonEdge edge = g_debouncer.update(pressed, now_ms);
  return g_gestures.update(edge, now_ms);
}

}  // namespace stackchan::input
