#include "stackchan/app/command_registry.hpp"

namespace stackchan::app {

bool ParamSpec::accepts(std::string_view value) const noexcept {
  if (choices == nullptr || choice_count == 0) {
    // A parameter with no listed values accepts anything.
    return true;
  }
  for (std::size_t i = 0; i < choice_count; ++i) {
    if (choices[i] == value) {
      return true;
    }
  }
  return false;
}

bool CommandRegistry::add(const CommandSpec& spec, CommandHandler handler,
                          void* context) noexcept {
  if (handler == nullptr || spec.name.empty() || count_ == kMaxCommands) {
    return false;
  }
  if (find(spec.name) != nullptr) {
    // Registering a name twice would leave it ambiguous which handler runs.
    return false;
  }
  entries_[count_] = Entry{spec, handler, context};
  ++count_;
  return true;
}

const CommandRegistry::Entry* CommandRegistry::find(
    std::string_view name) const noexcept {
  for (std::size_t i = 0; i < count_; ++i) {
    if (entries_[i].spec.name == name) {
      return &entries_[i];
    }
  }
  return nullptr;
}

domain::ErrorCode CommandRegistry::dispatch(
    std::string_view name, const ports::ParamReader& params,
    domain::JsonWriter& payload) const noexcept {
  const Entry* entry = find(name);
  if (entry == nullptr) {
    return domain::ErrorCode::unknown_command;
  }
  if (!entry->spec.available) {
    // Implemented, but not usable on this unit. Retrying changes nothing.
    return domain::ErrorCode::unsupported;
  }
  return entry->handler(entry->context, params, payload);
}

void CommandRegistry::write_capabilities(domain::JsonWriter& out) const noexcept {
  out.key("capabilities");
  out.begin_array();
  for (std::size_t i = 0; i < count_; ++i) {
    const CommandSpec& spec = entries_[i].spec;

    out.begin_object();
    out.member("name", spec.name);

    // Only the asynchronous ones are marked; synchronous is the default.
    if (spec.is_async) {
      out.member("async", true);
    }

    if (spec.param_count > 0) {
      out.key("params");
      out.begin_object();
      for (std::size_t p = 0; p < spec.param_count; ++p) {
        const ParamSpec& param = spec.params[p];
        out.key(param.name);
        if (param.choices != nullptr && param.choice_count > 0) {
          // List the permitted values, so a caller can learn them by
          // asking rather than by trial.
          out.begin_array();
          for (std::size_t c = 0; c < param.choice_count; ++c) {
            out.value(param.choices[c]);
          }
          out.end_array();
        } else {
          // Anything is accepted, so only the type is advertised.
          out.value(std::string_view{"string"});
        }
      }
      out.end_object();
    }

    // Unusable commands keep their entry. Removing it would force the
    // other end to distinguish absent from unavailable.
    if (!spec.available) {
      out.member("available", false);
      out.member("reason", spec.unavailable_reason);
    }

    out.end_object();
  }
  out.end_array();
}

}  // namespace stackchan::app
