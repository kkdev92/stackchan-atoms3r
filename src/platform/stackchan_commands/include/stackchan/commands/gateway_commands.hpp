#pragma once

#include "stackchan/commands/command_context.hpp"
#include "stackchan/conversation/gateway_client.hpp"

// gateway.* — where conversations are sent. Configured once before the
// first conversation.

namespace stackchan::commands {
namespace gateway_commands {

inline domain::ErrorCode handle_configure(void*, const ports::ParamReader& params,
                                          domain::JsonWriter& payload) {
  std::string_view url;
  if (!params.read_string("url", url)) {
    return domain::ErrorCode::invalid_argument;
  }
  if (!conversation::save_gateway_url(url)) {
  // Anything that is not http://, too long, or contains whitespace is
  // rejected here.
    return domain::ErrorCode::invalid_argument;
  }
  payload.begin_object();
  payload.member("url", conversation::gateway_url());
  payload.end_object();
  return domain::ErrorCode::none;
}

}  // namespace gateway_commands

// Register gateway.configure.
[[nodiscard]] inline bool register_gateway_commands(CommandContext& context) {
  using app::CommandSpec;
  using app::ParamSpec;
  static constexpr ParamSpec kParams[] = {{"url", nullptr, 0}};
  CommandSpec configure{"gateway.configure"};
  configure.params = kParams;
  configure.param_count = 1;
  return context.registry->add(configure, &gateway_commands::handle_configure,
                               &context);
}

}  // namespace stackchan::commands
