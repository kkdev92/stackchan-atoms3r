#pragma once

#include "stackchan/commands/command_context.hpp"
#include "esp_heap_caps.h"
#include "stackchan/app/device_describe.hpp"
#include "stackchan/app/health_payload.hpp"
#include "stackchan/domain/health.hpp"
#include "stackchan/identity/device.hpp"
#include "stackchan/net/wifi.hpp"

// device.* — identity, state and health.
//
// To add a family of commands, add a file like this one and a single line
// to register_commands.hpp.

#include "stackchan/conversation/task.hpp"
#include "stackchan/conversation/gateway_client.hpp"

namespace stackchan::commands {
namespace device_commands {

inline domain::ErrorCode handle_describe(void* context, const ports::ParamReader&,
                                         domain::JsonWriter& payload) {
  app::write_device_description(payload, identity::collect(),
                                *context_of(context).registry, identity::uptime_ms());
  return domain::ErrorCode::none;
}

// Refresh the state. Wi-Fi, the emergency stop and the conversation are
// owned elsewhere, so their values are copied in (invariant 6: owned by a
// task, read as a copy).
inline void refresh_state(CommandContext& deps) {
  domain::DeviceState& state = *deps.state;
  state.estop = deps.cancellation->token().emergency();
  state.network_connected =
      net::status().phase == domain::NetworkPhase::connected;
  state.conversation = conversation::phase();
  state.audio_busy = state.conversation != domain::ConversationPhase::idle;
  state.gateway_configured = !conversation::gateway_url().empty();
}

inline domain::ErrorCode handle_state(void* context, const ports::ParamReader&,
                                      domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  refresh_state(deps);
  const domain::DeviceState& state = *deps.state;
  payload.begin_object();
  payload.member("estop", state.estop);
  payload.member("conversation", domain::to_string(state.conversation));
  payload.member("audio_busy", state.audio_busy);
  payload.member("expression", domain::to_string(state.expression));
  payload.member("network_connected", state.network_connected);
  payload.member("can_start_conversation", state.can_start_conversation());
  // Always include why it cannot start, so the caller never has to guess.
  payload.member("blocked_by", state.why_cannot_start());
  payload.end_object();
  return domain::ErrorCode::none;
}

inline domain::ErrorCode handle_health(void*, const ports::ParamReader&,
                                       domain::JsonWriter& payload) {
  const domain::MemorySnapshot snapshot{
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)};
  // The thresholds are a decision in core; only the values are passed here.
  const domain::MemoryThresholds thresholds{80000, 40000, 20000};
  const auto level = domain::classify_memory(snapshot, thresholds);
  // Reading the numbers happens here, in the hardware; deciding their shape
  // happens in core, where a test can fix it.
  app::write_health_payload(
      payload, identity::uptime_ms(), snapshot,
      static_cast<std::uint64_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), level);
  return domain::ErrorCode::none;
}

}  // namespace device_commands

// Register device.describe, device.state and device.health.
[[nodiscard]] inline bool register_device_commands(CommandContext& context) {
  using app::CommandSpec;
  bool ok = context.registry->add(CommandSpec{"device.describe"},
                                  &device_commands::handle_describe, &context);
  ok = context.registry->add(CommandSpec{"device.state"},
                             &device_commands::handle_state, &context) &&
       ok;
  ok = context.registry->add(CommandSpec{"device.health"},
                             &device_commands::handle_health, &context) &&
       ok;
  return ok;
}

}  // namespace stackchan::commands
