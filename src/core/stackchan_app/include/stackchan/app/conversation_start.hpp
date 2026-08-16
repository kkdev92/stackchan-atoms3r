#pragma once

#include <cstddef>
#include <string_view>

#include "stackchan/domain/protocol.hpp"
#include "stackchan/ports/param_reader.hpp"

// Reading the arguments of conversation.start.
//
// An absent payload and an empty object deliberately mean "listen". Once a
// caller supplies any argument, however, it must supply a non-empty, readable
// text string. Treating an unreadable text as absent would turn a failed text
// request into a microphone recording.

namespace stackchan::app {

inline constexpr std::size_t kMaxConversationStartTextBytes = 480;

struct ConversationStartInput {
  std::string_view text{};

  [[nodiscard]] bool listens() const noexcept { return text.empty(); }
};

[[nodiscard]] inline domain::ErrorCode read_conversation_start_input(
    const ports::ParamReader& params, ConversationStartInput& out) noexcept {
  out.text = {};
  if (params.empty()) {
    return domain::ErrorCode::none;
  }
  if (!params.read_string("text", out.text) || out.text.empty() ||
      out.text.size() > kMaxConversationStartTextBytes) {
    return domain::ErrorCode::invalid_argument;
  }
  return domain::ErrorCode::none;
}

}  // namespace stackchan::app
