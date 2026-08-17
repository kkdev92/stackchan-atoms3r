#pragma once

#include "stackchan/app/conversation_start.hpp"
#include "stackchan/commands/command_context.hpp"
#include "stackchan/commands/device_commands.hpp"
#include "stackchan/conversation/task.hpp"

// conversation.* — starting and cancelling a conversation.
//
// Impossible on a unit without audio, so the entries stay and are
// advertised as unavailable with a reason.

namespace stackchan::commands {
namespace conversation_commands {

// Start a conversation. The recording, sending and playback are carried out
// by the conversation task.
//
// Passing text skips recording and proceeds as though those words had been
// heard, which exercises the whole path without a microphone.
inline domain::ErrorCode handle_start(void* context,
                                      const ports::ParamReader& params,
                                      domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  app::ConversationStartInput input{};
  const domain::ErrorCode input_error =
      app::read_conversation_start_input(params, input);
  if (input_error != domain::ErrorCode::none) {
    return input_error;
  }

  device_commands::refresh_state(deps);
  if (!deps.state->can_start_conversation()) {
    // A specific code per reason, because it changes whether the caller
    // should retry.
    if (deps.state->estop) {
      return domain::ErrorCode::estop_engaged;
    }
    if (deps.state->conversation != domain::ConversationPhase::idle ||
        deps.state->audio_busy) {
      return domain::ErrorCode::busy;
    }
    return domain::ErrorCode::unavailable;
  }
  if (!conversation::request_start(input.text)) {
    return domain::ErrorCode::busy;
  }
  payload.begin_object();
  payload.member("accepted", true);
  payload.member("mode", input.listens() ? "listen" : "text");
  payload.end_object();
  return domain::ErrorCode::none;
}

// Cancel the conversation in progress. Unlike an emergency stop, the next
// one can begin immediately.
inline domain::ErrorCode handle_cancel(void* context, const ports::ParamReader&,
                                       domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  const bool running =
      conversation::phase() != domain::ConversationPhase::idle;
  if (running) {
    deps.cancellation->cancel(runtime::CancelReason::requested);
  }
  payload.begin_object();
  payload.member("cancelled", running);
  payload.end_object();
  return domain::ErrorCode::none;
}

}  // namespace conversation_commands

// Register conversation.start and conversation.cancel.
[[nodiscard]] inline bool register_conversation_commands(CommandContext& context) {
  using app::CommandSpec;
  using app::ParamSpec;

  static constexpr ParamSpec kStartParams[] = {{"text", nullptr, 0}};
  CommandSpec start{"conversation.start"};
  start.params = kStartParams;
  start.param_count = 1;
  start.is_async = true;  // the reply only acknowledges; follow device.state
  start.available = context.voice_available;
  start.unavailable_reason = "voice_base_not_detected";
  bool ok = context.registry->add(start, &conversation_commands::handle_start,
                                  &context);

  CommandSpec cancel{"conversation.cancel"};
  cancel.available = context.voice_available;
  cancel.unavailable_reason = "voice_base_not_detected";
  ok = context.registry->add(cancel, &conversation_commands::handle_cancel,
                             &context) &&
       ok;
  return ok;
}

}  // namespace stackchan::commands
