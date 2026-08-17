#pragma once

#include "stackchan/commands/command_context.hpp"
#include "stackchan/commands/device_commands.hpp"
#include "esp_log.h"
#include "stackchan/commands/estop_commands.hpp"
#include "stackchan/commands/face_commands.hpp"
#include "stackchan/commands/gateway_commands.hpp"
#include "stackchan/commands/conversation_commands.hpp"
#include "stackchan/commands/token_commands.hpp"

// Assembles every command. Adding a family means an include and one line
// here.
//
// Registration is the only entry point: what is listed here is what gets
// dispatched, what appears in the capabilities, and what the other end can
// discover.

namespace stackchan::commands {

[[nodiscard]] inline bool register_commands(CommandContext& context) {
  bool ok = register_device_commands(context);
  ok = register_estop_commands(context) && ok;
  ok = register_face_commands(context) && ok;
  ok = register_gateway_commands(context) && ok;
  ok = register_conversation_commands(context) && ok;
  ok = register_token_commands(context) && ok;
  if (!ok) {
    ESP_LOGE("commands", "some commands could not be registered");
  }
  return ok;
}

}  // namespace stackchan::commands
