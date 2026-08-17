#pragma once

#include "stackchan/app/command_registry.hpp"
#include "stackchan/domain/device_state.hpp"
#include "stackchan/ports/face.hpp"
#include "stackchan/runtime/cancellation.hpp"

// The dependencies every command handler shares.
//
// Handlers are plain function pointers and cannot capture, so their state
// arrives through a context pointer. Rather than adding globals for each
// family of commands, the main function owns this one bundle and passes the
// same pointer to all of them.
//
// None of the fields are owned here; main keeps them alive.

namespace stackchan::commands {

struct CommandContext {
  app::CommandRegistry* registry = nullptr;  // describe reads what is registered
  ports::Face* face = nullptr;
  runtime::CancellationSource* cancellation = nullptr;
  domain::DeviceState* state = nullptr;

  // Fixed when the registry is assembled. Rather than removing an entry,
  // an unusable command is advertised with a reason (invariant 3).
  bool display_available = false;
  bool voice_available = false;
};

[[nodiscard]] inline CommandContext& context_of(void* context) {
  return *static_cast<CommandContext*>(context);
}

}  // namespace stackchan::commands
