#pragma once

#include "stackchan/commands/command_context.hpp"
#include "stackchan/domain/expression.hpp"

// face.* — expressions. The argument is validated against the same table
// the capabilities are generated from, so what is advertised and what is
// accepted cannot disagree (invariant 3).

namespace stackchan::commands {
namespace face_commands {

inline domain::ErrorCode handle_set_expression(void* context,
                                               const ports::ParamReader& params,
                                               domain::JsonWriter& payload) {
  CommandContext& deps = context_of(context);
  // Nothing moves during an emergency stop; moving would defeat it.
  if (deps.cancellation->token().emergency()) {
    return domain::ErrorCode::estop_engaged;
  }
  std::string_view name;
  if (!params.read_string("expression", name)) {
    return domain::ErrorCode::invalid_argument;
  }
  const auto expression = domain::parse_expression(name);
  if (!expression.has_value()) {
    return domain::ErrorCode::invalid_argument;
  }
  deps.face->show(*expression);
  deps.state->expression = *expression;
  payload.begin_object();
  payload.member("expression", name);
  payload.end_object();
  return domain::ErrorCode::none;
}

}  // namespace face_commands

// Register face.set_expression.
[[nodiscard]] inline bool register_face_commands(CommandContext& context) {
  using app::CommandSpec;
  using app::ParamSpec;
  static constexpr ParamSpec kExpressionParams[] = {
      {"expression", domain::kExpressionNames.data(), domain::kExpressionCount}};
  CommandSpec set_expression{"face.set_expression"};
  set_expression.params = kExpressionParams;
  set_expression.param_count = 1;
  // With no working display, advertise it as unavailable rather than
  // removing the entry (invariant 3).
  set_expression.available = context.display_available;
  set_expression.unavailable_reason = "display_not_detected";
  return context.registry->add(set_expression,
                               &face_commands::handle_set_expression, &context);
}

}  // namespace stackchan::commands
