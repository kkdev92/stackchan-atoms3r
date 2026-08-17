#pragma once

#include "stackchan/commands/command_context.hpp"
#include "esp_log.h"

// estop.* — the emergency stop. It remains active until explicitly cleared
// during the current boot; restarting the device resets this in-memory state.

namespace stackchan::commands {
namespace estop_commands {

inline domain::ErrorCode handle_engage(void* context, const ports::ParamReader&,
                                       domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  deps.cancellation->cancel(runtime::CancelReason::emergency_stop);
  ESP_LOGW("commands", "emergency stop engaged");
  payload.begin_object();
  payload.member("estop", true);
  payload.end_object();
  return domain::ErrorCode::none;
}

inline domain::ErrorCode handle_clear(void* context, const ports::ParamReader&,
                                      domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  deps.cancellation->clear_emergency();
  ESP_LOGI("commands", "emergency stop cleared");
  payload.begin_object();
  payload.member("estop", deps.cancellation->token().emergency());
  payload.end_object();
  return domain::ErrorCode::none;
}

}  // namespace estop_commands

// Register estop.engage and estop.clear.
[[nodiscard]] inline bool register_estop_commands(CommandContext& context) {
  using app::CommandSpec;
  bool ok = context.registry->add(CommandSpec{"estop.engage"},
                                  &estop_commands::handle_engage, &context);
  ok = context.registry->add(CommandSpec{"estop.clear"},
                             &estop_commands::handle_clear, &context) &&
       ok;
  return ok;
}

}  // namespace stackchan::commands
