#pragma once

#include <cstdint>
#include <string_view>

#include "stackchan/domain/expression.hpp"

// Everything about what the device is doing right now, in one place.
//
// Why it is one struct
// --------------------
// Several tasks want to know whether a conversation is running, whether the
// network is up, whether the emergency stop is engaged. Spread across
// separate globals, those answers can disagree with each other — read two
// of them a microsecond apart and you get a picture that was never true.
//
// Gathered here, the state is owned by one task and everyone else takes a
// copy. That is invariant 6, do not add global mutable state, in practice.
//
// What is deliberately absent
// ---------------------------
// No addresses or ports for speech recognition or synthesis. Those belong
// to the gateway, and the device does not know what it is talking to beyond
// the one URL it posts to.

namespace stackchan::domain {

// Where a conversation currently is.
enum class ConversationPhase : std::uint8_t {
  idle,       // nothing in progress
  listening,  // recording
  thinking,   // waiting for the gateway to answer
  speaking,   // playing the reply
};

[[nodiscard]] std::string_view to_string(ConversationPhase phase) noexcept;

struct DeviceState {
  // Emergency stop engaged. No new work is accepted until it is released.
  bool estop = false;

  ConversationPhase conversation = ConversationPhase::idle;

  // Something is using the audio hardware, recording or playing.
  bool audio_busy = false;

  Expression expression = Expression::neutral;

  // Whether the network is up. A conversation cannot start without it.
  bool network_connected = false;

  // Whether a gateway address has been configured. The address itself is
  // not kept here: only its presence affects any decision.
  bool gateway_configured = false;

  // The most recent failure, or empty. A string, so that a person reading
  // it learns something.
  //
  // No initializer, unlike the members above: those are a bool and an enum,
  // which default-initialize to whatever was in the storage. A string_view
  // constructs itself empty, so writing = {} here would say nothing.
  std::string_view last_error;

  // Whether a new conversation may start.
  //
  // The check lives here so that every entry point — the API, the button,
  // anything added later — agrees on the answer. Duplicating the conditions
  // at each call site is how they drift apart.
  [[nodiscard]] bool can_start_conversation() const noexcept {
    return !estop && conversation == ConversationPhase::idle && !audio_busy &&
           network_connected && gateway_configured;
  }

  // Why it may not start, for when can_start_conversation() is false.
  //
  // Provided so that callers report the actual reason instead of guessing
  // at one.
  [[nodiscard]] std::string_view why_cannot_start() const noexcept;
};

}  // namespace stackchan::domain
