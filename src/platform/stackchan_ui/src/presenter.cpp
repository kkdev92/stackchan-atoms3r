#include "stackchan/ui/presenter.hpp"

#include <array>

#include "esp_log.h"
#include "stackchan/app/device_describe.hpp"
#include "stackchan/conversation/gateway_client.hpp"
#include "stackchan/conversation/task.hpp"
#include "stackchan/domain/json_writer.hpp"
#include "stackchan/identity/device.hpp"
#include "stackchan/net/wifi.hpp"
#include "stackchan/probe/hardware.hpp"

namespace stackchan::ui {
namespace {

constexpr char kTag[] = "ui";

// How long a notice stays on screen. The loop is never blocked for this;
// only a deadline is recorded and the loop keeps running, so the button and
// the API stay alive while a notice is displayed.
constexpr std::uint32_t kNoticeMs = 7000;
constexpr std::uint32_t kGuideMs = 6000;

}  // namespace

void Presenter::boot_screen() {
  if (!net::has_credentials() && net::status().access_point_up) {
    // Without credentials, show the setup screen rather than the face so the
    // connection details are available.
    show_pairing();
  } else {
    show_face();
  }
}

void Presenter::handle(const domain::Gesture& gesture, std::uint32_t now_ms) {
  switch (gesture.kind) {
    case domain::GestureKind::hold:
      on_hold();
      break;
    case domain::GestureKind::clicks:
      ESP_LOGI(kTag, "button: %u click(s)", static_cast<unsigned>(gesture.clicks));
      if (gesture.clicks == 1) {
        on_single_click(now_ms);
      } else if (gesture.clicks == 2) {
        on_double_click(now_ms);
      } else if (gesture.clicks == 3) {
        on_triple_click(now_ms);
      }
      // Four or more presses do nothing.
      break;
    case domain::GestureKind::none:
      break;
  }
}

void Presenter::tick(std::uint32_t now_ms) {
  // The notice deadline, compared as a signed difference so it stays
  // correct across the counter wrapping.
  if (notice_shown_ &&
      static_cast<std::int32_t>(now_ms - notice_until_ms_) >= 0) {
    show_face();
  }

  // When a conversation ends, return from the per-sentence expression to
  // the resting one.
  //
  // A per-sentence expression belongs to the sentence being spoken. Left in
  // place, the last sentence's face becomes the device's permanent
  // expression.
  const auto phase_now = conversation::phase();
  if (phase_now == domain::ConversationPhase::idle &&
      last_phase_ != domain::ConversationPhase::idle) {
    show_face();
  }
  last_phase_ = phase_now;

  // Once configured and connected, go from the setup screen back to the
  // face.
  const bool connected =
      net::status().phase == domain::NetworkPhase::connected;
  if (connected && !was_connected_ && tick_started_) {
    show_face();
  }
  was_connected_ = connected;

  // Show the setup screen when a recovery access point comes up so its
  // passphrase and connection details are available.
  const bool ap_up = net::status().access_point_up;
  if (ap_up && !was_ap_up_ && tick_started_) {
    show_pairing();
  }
  was_ap_up_ = ap_up;
  tick_started_ = true;
}

// ------------------------------------------------------------- the screens

void Presenter::show_face() {
  notice_shown_ = false;
  deps_.face->show(deps_.state->expression);
}

void Presenter::show_notice(std::string_view text, std::uint32_t now_ms,
                            std::uint32_t for_ms) {
  deps_.face->show_message(text);
  notice_shown_ = true;
  notice_until_ms_ = now_ms + for_ms;
}

void Presenter::show_pairing() {
  deps_.face->show_pairing(net::status().ap_ssid, net::status().ap_password,
                           "192.168.4.1");

  // If no screen is available, provide the passphrase through the serial port
  // so setup remains possible with physical USB access. Do not log it when the
  // display is available.
  if (!deps_.screen_available) {
    ESP_LOGW(kTag, "no display; setup key for %s is %s", net::status().ap_ssid,
             net::status().ap_password);
  }
}

// A confirmation beep. Never during a conversation: besides interrupting
// the speech, the microphone and speaker cannot be used at once.
void Presenter::beep(std::uint32_t hz, std::uint32_t duration_ms) {
  if (deps_.voice == nullptr || !deps_.voice->available()) {
    return;
  }
  if (conversation::phase() != domain::ConversationPhase::idle) {
    return;
  }
  (void)deps_.voice->play_tone(hz, duration_ms);
}

void Presenter::log_description() {
  if (deps_.registry == nullptr) {
    return;
  }
  // A function-local static suffices, because only the main loop calls
  // this.
  static std::array<char, 1024> buffer{};
  domain::JsonWriter payload{buffer.data(), buffer.size()};
  app::write_device_description(payload, identity::collect(), *deps_.registry,
                                identity::uptime_ms());
  if (!payload.valid()) {
    // Do not print truncated JSON on overflow; report the size needed.
    ESP_LOGE(kTag, "describe overflowed: needed %u bytes",
             static_cast<unsigned>(payload.required()));
    return;
  }
  ESP_LOGI(kTag, "describe %.*s", static_cast<int>(payload.length()),
           payload.text().data());
}

void Presenter::refresh_state() {
  domain::DeviceState& state = *deps_.state;
  state.estop = deps_.cancellation->token().emergency();
  state.network_connected =
      net::status().phase == domain::NetworkPhase::connected;
  state.conversation = conversation::phase();
  state.audio_busy = state.conversation != domain::ConversationPhase::idle;
  state.gateway_configured = !conversation::gateway_url().empty();
}

// ------------------------------------------------------ one press at a time

// One press: start a conversation.
void Presenter::on_single_click(std::uint32_t now_ms) {
  refresh_state();
  if (deps_.state->can_start_conversation() && conversation::request_start()) {
    ESP_LOGI(kTag, "button: conversation start");
    return;
  }

  const std::string_view reason = deps_.state->why_cannot_start();
  ESP_LOGI(kTag, "button: cannot start a conversation (%.*s)",
           static_cast<int>(reason.size()), reason.data());
  beep(500, 100);

  // Show something different for each reason it cannot start.
  const auto& net_status = net::status();
  if (!deps_.state->network_connected && net_status.access_point_up) {
    show_pairing();
    notice_shown_ = true;
    notice_until_ms_ = now_ms + kGuideMs;
  } else if (!deps_.state->network_connected) {
    show_notice("NO WIFI", now_ms, kNoticeMs);
  } else if (!deps_.state->gateway_configured) {
    show_notice("NO GATEWAY", now_ms, kNoticeMs);
  }
  // During a conversation or an emergency stop, leave the screen alone:
  // the face is already showing it.
}

// Two presses: what hardware is attached.
//
// Reports what was found at boot rather than probing again. The codec
// holds the I2C bus, so a scan now would fail to open it and report that
// nothing is connected.
void Presenter::on_double_click(std::uint32_t now_ms) {
  const auto& hardware = probe::detect();
  ESP_LOGI(kTag, "button: internal=%u devices (imu=%d) external=%u devices (voice=%d)",
           static_cast<unsigned>(hardware.internal_device_count),
           hardware.motion_sensor_identified ? 1 : 0,
           static_cast<unsigned>(hardware.external_device_count),
           hardware.codec_identified ? 1 : 0);

  if (hardware.codec_identified && hardware.motion_sensor_identified) {
    show_notice("VOICE+IMU", now_ms, kNoticeMs);
  } else if (hardware.codec_identified) {
    show_notice("VOICE", now_ms, kNoticeMs);
  } else if (hardware.motion_sensor_identified) {
    show_notice("IMU", now_ms, kNoticeMs);
  } else {
    show_notice("NO SENSOR", now_ms, kNoticeMs);
    beep(800, 120);
  }
}

// Three presses: the current connection.
void Presenter::on_triple_click(std::uint32_t now_ms) {
  const auto& net_status = net::status();
  show_notice(net_status.ip[0] != 0 ? std::string_view{net_status.ip}
                                    : std::string_view{"NO IP"},
              now_ms, kNoticeMs);
  beep(500, 100);

  // The serial port can carry more detail than the screen.
  const std::string_view url = conversation::gateway_url();
  ESP_LOGI(kTag, "gateway url: %.*s", static_cast<int>(url.size()),
           url.empty() ? "(not set)" : url.data());
  log_description();
}

// Long press: stop the speech, or print diagnostics if nothing is being
// said.
void Presenter::on_hold() {
  if (conversation::phase() != domain::ConversationPhase::idle) {
    deps_.cancellation->cancel(runtime::CancelReason::requested);
    ESP_LOGI(kTag, "button: conversation cancelled");
    return;
  }
  // When silent, print diagnostics. This is how someone who attached a
  // terminal after boot gets the device's details.
  ESP_LOGI(kTag, "button: held");
  log_description();

  // An idle hold provides token recovery through the serial port without
  // logging the token on every boot. It requires physical button and USB
  // access.
  if (deps_.token != nullptr && deps_.token->is_set()) {
    const std::string_view text = deps_.token->text();
    ESP_LOGW(kTag, "access token: %.*s", static_cast<int>(text.size()), text.data());
    ESP_LOGW(kTag, "use it as the X-StackChan-Token header; keep it out of shared logs");
  }
}

}  // namespace stackchan::ui
