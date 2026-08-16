#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/app/conversation.hpp"
#include "stackchan/runtime/cancellation.hpp"

// The client that talks to the gateway.
//
//   POST a WAV to {gateway_url}/v1/converse
//   feed the event-stream response straight into a ConversationTurn
//
// Nothing here interprets the response: that is ConversationTurn's job, in
// core, where it is covered by host tests. This part only carries bytes,
// with deadlines and cancellation attached.
//
// Deadlines
// ---------
// Ten seconds to receive response headers, then thirty seconds without stream
// data (keep-alives count). Exceeding either closes the connection and reports
// a timeout. Closing the connection is also how cancellation reaches the
// gateway.

namespace stackchan::conversation {

// The gateway's address, kept in NVS.
//
// Load once at startup. Setting it stores and applies it in one step.
inline constexpr std::size_t kMaxUrlLength = 120;

void load_gateway_url();
[[nodiscard]] bool save_gateway_url(std::string_view url);
// Empty when nothing has been configured.
[[nodiscard]] std::string_view gateway_url();

struct ConverseRequest {
  // The PCM to send; the WAV header is added here.
  const std::uint8_t* pcm = nullptr;
  std::size_t pcm_bytes = 0;
  // Words to say. When this is not empty it is sent **instead of** audio,
  // as JSON rather than a WAV. The response is identical either way, so
  // nothing downstream changes.
  std::string_view text;
  // Identifies this conversation, sent as a header.
  std::string_view conversation_id;
};

// One conversation. The return value says whether the exchange completed
// as a piece of network traffic.
//
// What actually happened — completed, cancelled, failed — is in
// turn.outcome(). When the transport breaks, this calls turn.abort() before
// returning false, so the listener always receives exactly one on_finished.
[[nodiscard]] bool converse(const ConverseRequest& request,
                            app::ConversationTurn& turn,
                            runtime::CancellationToken token);

}  // namespace stackchan::conversation
