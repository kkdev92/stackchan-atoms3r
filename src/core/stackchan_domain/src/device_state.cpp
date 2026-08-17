#include "stackchan/domain/device_state.hpp"

namespace stackchan::domain {

std::string_view to_string(ConversationPhase phase) noexcept {
  switch (phase) {
    case ConversationPhase::idle:
      return "idle";
    case ConversationPhase::listening:
      return "listening";
    case ConversationPhase::thinking:
      return "thinking";
    case ConversationPhase::speaking:
      return "speaking";
  }
  return "idle";
}

std::string_view DeviceState::why_cannot_start() const noexcept {
  // Checked in order of severity, so an emergency stop is reported ahead of
  // anything else: it is released differently, and a caller told the wrong
  // reason will retry forever.
  if (estop) {
    return "emergency stop is engaged";
  }
  if (conversation != ConversationPhase::idle) {
    return "a conversation is already running";
  }
  if (audio_busy) {
    return "audio is in use";
  }
  if (!network_connected) {
    return "not connected to a network";
  }
  if (!gateway_configured) {
    return "gateway url is not configured";
  }
  return "";
}

}  // namespace stackchan::domain
