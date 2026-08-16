// Wiring, and nothing else. No decisions and no drawing happen here.
//
//   src/core      the decisions. Never touches ESP-IDF, so all of it is
//                 tested on a PC, with no board attached
//   src/platform  the hardware and the presentation. One responsibility per
//                 component
//   here          joins the two. The only place that knows both a concrete
//                 thing and the abstraction it satisfies
//
// Where to add things
//   a command        a file in stackchan_commands, and one line to register
//                    it
//   an HTTP route    a file in stackchan_deviceapi, and one install line
//   button or screen behaviour   stackchan_ui/presenter.cpp
//
// What belongs here is starting a new component and handing it its
// dependencies. If a change to this file needs an if statement, it probably
// belongs somewhere else.
//
// The order below is not arbitrary; several steps have to happen before
// others, and each says why.

#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "stackchan/app/request_router.hpp"
#include "stackchan/audio/voice_base.hpp"
#include "stackchan/board/profile.hpp"
#include "stackchan/commands/register_commands.hpp"
#include "stackchan/conversation/gateway_client.hpp"
#include "stackchan/conversation/task.hpp"
#include "stackchan/deviceapi/server.hpp"
#include "stackchan/display/m5gfx_face.hpp"
#include "stackchan/identity/device.hpp"
#include "stackchan/identity/token.hpp"
#include "stackchan/input/button.hpp"
#include "stackchan/net/wifi.hpp"
#include "stackchan/probe/hardware.hpp"
#include "stackchan/selftest/selftest.hpp"
#include "stackchan/ui/presenter.hpp"

namespace {

constexpr char kTag[] = "bootstrap";

// How often memory is logged. Watching this over a long run is how a leak
// would show up.
constexpr std::uint32_t kMemoryLogIntervalMs = 5000;
// How often the screen and the button are serviced. Comfortably finer than
// the debounce interval.
constexpr std::uint32_t kTickMs = 10;

stackchan::display::M5GfxFace g_face;
stackchan::audio::VoiceBase g_voice;
stackchan::app::CommandRegistry g_commands;
stackchan::runtime::CancellationSource g_cancellation;
stackchan::domain::DeviceState g_state;
stackchan::commands::CommandContext g_context{&g_commands, &g_face, &g_cancellation,
                                              &g_state, false, false};

[[nodiscard]] std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void log_memory() {
  const std::size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const std::size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const std::size_t largest_internal =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  // The largest DMA-capable block is watched as well as the total. The
  // Wi-Fi driver allocates from it, and it can run out while the total
  // still looks healthy — fragmentation is invisible in the total alone.
  const std::size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
  ESP_LOGI(kTag,
           "memory internal_free=%u largest_internal=%u largest_dma=%u psram_free=%u",
           static_cast<unsigned>(internal_free),
           static_cast<unsigned>(largest_internal),
           static_cast<unsigned>(largest_dma), static_cast<unsigned>(psram_free));
}

}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "StackChan firmware bootstrap");
  ESP_LOGI(kTag, "PSRAM initialized=%s", esp_psram_is_initialized() ? "true" : "false");
  log_memory();

  // Check the declared pin assignment against how this unit is configured.
  stackchan::board::BoardProfile profile;
  profile.bottom_header = stackchan::board::BottomHeaderUsage::voice_base;
  const auto conflict = stackchan::board::validate(profile);
  ESP_LOGI(kTag, "board profile: %s",
           conflict.has_value() ? conflict->data() : "consistent");

  // Find out what is attached before initialising the display: the display
  // driver opens I2C for backlight control, which collides with the scan.
  stackchan::probe::log_scan();

  // Carry on without a display. The logs are still worth having.
  if (!g_face.begin()) {
    ESP_LOGE(kTag, "display init failed");
  } else {
    ESP_LOGI(kTag, "display %dx%d board=%d", g_face.width(), g_face.height(),
             g_face.board_id());
  }

  if (!stackchan::input::begin()) {
    ESP_LOGE(kTag, "button could not be configured");
  }

  // The API token, **before Wi-Fi**: generating it uses the entropy source,
  // which shares the analogue front end with the radio.
  const auto& token = stackchan::identity::access_token();
  if (!token.is_set()) {
    ESP_LOGE(kTag, "no access token; the api will refuse everything");
  } else if (stackchan::identity::token_is_new()) {
    ESP_LOGW(kTag, "new access token: %.*s", static_cast<int>(token.text().size()),
             token.text().data());
    ESP_LOGW(kTag, "use it as the X-StackChan-Token header and store it securely");
  } else {
    ESP_LOGI(kTag, "access token loaded");
  }

  // Audio, also before Wi-Fi, because the codec's control bus is the same
  // one the scan used.
  if (!g_voice.begin()) {
    ESP_LOGW(kTag, "voice base not available; audio commands will be refused");
  } else {
    // Tone first, then the microphone check. The other order switches the
    // amplifier on and off twice.
    const std::size_t played = g_voice.play_tone(1000, 200);
    ESP_LOGI(kTag, "startup tone: %u samples written",
             static_cast<unsigned>(played));
    const auto mic = g_voice.check_microphone();
    ESP_LOGI(kTag, "microphone: %u samples, rms=%u -> %s",
             static_cast<unsigned>(mic.samples), static_cast<unsigned>(mic.rms),
             mic.receiving() ? "receiving" : "NO DATA");
  }

  // Wi-Fi. Nothing waits for it to connect; waiting would stop the display
  // and the button.
  if (!stackchan::net::begin()) {
    ESP_LOGE(kTag, "wifi could not be started");
  }

  // Conversation. Read the destination, and start the task only on a unit
  // that has audio.
  stackchan::conversation::load_gateway_url();
  if (g_voice.available()) {
    stackchan::conversation::TaskDeps conversation_deps;
    conversation_deps.source = &g_voice;
    conversation_deps.sink = &g_voice;
    conversation_deps.face = &g_face;
    conversation_deps.cancellation = &g_cancellation;
    if (!stackchan::conversation::begin(conversation_deps)) {
      ESP_LOGE(kTag, "conversation task could not be started");
    }
  }

  // The commands. What is available follows from what was assembled
  // (invariant 3).
  g_context.display_available = g_face.available();
  g_context.voice_available = g_voice.available();
  (void)stackchan::commands::register_commands(g_context);

  static stackchan::app::RequestRouter router{g_commands, token};

  // Check that decisions fixed by the host tests behave the same here.
  //
  // **Before the server opens.** RequestRouter carries internal buffers and
  // cannot be called from two tasks at once, so starting the server first
  // would let an incoming request collide with this check.
  (void)stackchan::selftest::run(router, token);

  // The server. Decisions belong to the router; this is wiring. From here
  // on, the router is only ever called from the server's task.
  if (!stackchan::deviceapi::begin(router, stackchan::identity::collect())) {
    ESP_LOGE(kTag, "device api could not be started");
  }

  // The screen and button behaviour, built after the token exists, because
  // a long press can print it.
  stackchan::ui::Presenter presenter{stackchan::ui::PresenterDeps{
      &g_face, &g_voice, &g_cancellation, &g_state, &g_commands,
      g_face.available(), &token}};

  presenter.boot_screen();
  ESP_LOGI(kTag, "ready. press the button.");

  std::uint32_t last_memory_log = now_ms();
  while (true) {
    const std::uint32_t t = now_ms();

    presenter.handle(stackchan::input::poll(t), t);
    presenter.tick(t);

    // Animate the face. Redraws only while the face is the current screen.
    g_face.tick(t);

    // Reconnection, and raising or dropping the access point. The timing is
    // decided by the network policy.
    stackchan::net::tick(t);

    if (t - last_memory_log >= kMemoryLogIntervalMs) {
      last_memory_log = t;
      log_memory();
    }

    vTaskDelay(pdMS_TO_TICKS(kTickMs));
  }
}
