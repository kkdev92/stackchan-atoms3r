#pragma once

#include "stackchan/commands/command_context.hpp"
#include "stackchan/conversation/task.hpp"
#include "stackchan/identity/token.hpp"
#include "stackchan/net/wifi.hpp"

// token.* — withdrawing the access token.
//
// The token is generated once and kept, which is what makes it usable: a
// value that changed on every restart would have to be redistributed to
// every client after each power cut. The cost of keeping it is that a value
// which has leaked stays valid, and until this command existed the only
// remedy was clearing NVS — which also takes the Wi-Fi credentials with it,
// because they deliberately share a namespace so that a factory reset clears
// both.
//
// What this is, and is not, protection against
// --------------------------------------------
// It answers a token that escaped **out of band**: pasted into an issue
// along with a startup log, caught in a screenshot, or given to somebody who
// no longer needs it.
//
// It does not answer somebody watching the network. Nothing here does: the
// device API is plain HTTP, so every command already carries the token in
// clear text. Anyone positioned to read the reply to this command could read
// the token in the request that follows it. Say so plainly rather than
// implying a protection that is not there.

namespace stackchan::commands {
namespace token_commands {

inline domain::ErrorCode handle_rotate(void*, const ports::ParamReader&,
                                       domain::JsonWriter& payload) {
  // Refused while a conversation is running, because the conversation task
  // reads the token on its own task when it posts to the gateway. Waiting
  // avoids handing it a half-replaced value.
  //
  // If the token has to be withdrawn *now* — the reason for rotating is
  // rarely patient — engage the emergency stop first. That is always
  // accepted, ends the conversation, and leaves this free to run.
  if (conversation::phase() != domain::ConversationPhase::idle) {
    return domain::ErrorCode::busy;
  }

  // Refused unless the radio is actually running, because that is what makes
  // esp_random() a true random source rather than a pseudo-random sequence
  // (ESP-IDF's esp_random.h says so, and the boot path enables the entropy
  // source explicitly for exactly this reason).
  //
  // Today this cannot fire: the request arrived over Wi-Fi, so the radio is
  // up by construction. It is here because that will not always be true.
  // This contract says the transport may change, and an HTTP request over
  // Ethernet or USB would reach this code with the radio off — and would
  // then quietly mint a guessable token. A refusal is the right answer to
  // "the entropy this needs is not available"; silence is not.
  //
  // Either interface being up means esp_wifi_start() ran. Neither being up
  // means no request could have arrived over Wi-Fi in the first place, so
  // this never wrongly refuses a real one.
  const net::Status& network = net::status();
  if (!network.access_point_up && network.ip[0] == '\0') {
    return domain::ErrorCode::unavailable;
  }

  if (!identity::rotate_access_token()) {
    return domain::ErrorCode::internal;
  }

  // The new token goes in the reply. The caller authenticated with the old
  // one over this same clear-text connection a moment ago, so returning it
  // exposes nothing that request did not already expose — and withholding
  // it would lock out anyone rotating from somewhere other than the serial
  // console. It is logged as well, so it is recoverable if the reply is
  // lost.
  payload.begin_object();
  payload.member("token", identity::access_token().text());
  payload.end_object();
  return domain::ErrorCode::none;
}

}  // namespace token_commands

// Register token.rotate.
[[nodiscard]] inline bool register_token_commands(CommandContext& context) {
  using app::CommandSpec;
  return context.registry->add(CommandSpec{"token.rotate"},
                               &token_commands::handle_rotate, &context);
}

}  // namespace stackchan::commands
