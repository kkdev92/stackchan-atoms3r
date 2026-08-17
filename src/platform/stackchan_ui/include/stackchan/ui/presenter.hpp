#pragma once

#include <cstdint>

#include "stackchan/app/command_registry.hpp"
#include "stackchan/domain/access_token.hpp"
#include "stackchan/audio/voice_base.hpp"
#include "stackchan/domain/button_gesture.hpp"
#include "stackchan/domain/device_state.hpp"
#include "stackchan/ports/face.hpp"
#include "stackchan/runtime/cancellation.hpp"

// What the screen shows and how the button behaves, gathered in one place.
//
// The button carries four actions:
//   one press    start a conversation, or show why it cannot start
//   two          what hardware is attached
//   three        the current connection
//   long press   stop the speech, or print diagnostics if it is silent
//
// The parts that can be decided without hardware — reading the presses,
// judging whether a conversation may start — already live in core. What is
// left here is choosing which screen to show and when, which needs the real
// probe, network and conversation state, and so belongs to platform.

namespace stackchan::ui {

struct PresenterDeps {
  ports::Face* face = nullptr;
  audio::VoiceBase* voice = nullptr;  // confirmation beeps; silent without one
  runtime::CancellationSource* cancellation = nullptr;
  domain::DeviceState* state = nullptr;
  // Read to print diagnostics to the serial port. Skipped when absent.
  const app::CommandRegistry* registry = nullptr;
  // Whether setup details can be shown on a working display. Without one, the
  // presenter uses the serial port as a physical-access fallback.
  bool screen_available = true;

  // Used to recover the token by long press. Skipped when absent.
  const domain::AccessToken* token = nullptr;
};

class Presenter {
 public:
  explicit Presenter(const PresenterDeps& deps) noexcept : deps_(deps) {}

  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;
  Presenter(Presenter&&) = delete;
  Presenter& operator=(Presenter&&) = delete;
  ~Presenter() = default;

  // The first screen. Setup instructions if there are no credentials, the
  // face otherwise.
  void boot_screen();

  // React to a press.
  void handle(const domain::Gesture& gesture, std::uint32_t now_ms);

  // Per-iteration screen decisions: expiring a notice, restoring the
  // expression after a conversation, and switching screens when the
  // network connects or an access point comes up.
  void tick(std::uint32_t now_ms);

 private:
  void show_face();
  void show_notice(std::string_view text, std::uint32_t now_ms, std::uint32_t for_ms);
  void show_pairing();
  void beep(std::uint32_t hz, std::uint32_t duration_ms);
  void refresh_state();

  // Print this unit's identity and its available operations to the serial
  // port.
  //
  // The button path makes this available after a terminal attaches, even if
  // the startup log was missed.
  void log_description();

  void on_single_click(std::uint32_t now_ms);
  void on_double_click(std::uint32_t now_ms);
  void on_triple_click(std::uint32_t now_ms);
  void on_hold();

  PresenterDeps deps_;

  // When the current notice expires. False when none is showing.
  bool notice_shown_ = false;
  std::uint32_t notice_until_ms_ = 0;

  // Previous values, for spotting transitions.
  domain::ConversationPhase last_phase_ = domain::ConversationPhase::idle;
  bool was_connected_ = false;
  bool was_ap_up_ = false;
  bool tick_started_ = false;
};

}  // namespace stackchan::ui
