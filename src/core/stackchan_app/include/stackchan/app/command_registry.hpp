#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/domain/json_writer.hpp"
#include "stackchan/domain/protocol.hpp"
#include "stackchan/ports/param_reader.hpp"

// Where the device's operations are registered.
//
// Why registration is the only entry point
// ----------------------------------------
// Registering a handler and advertising that it exists are the same act
// here. Add one, and all three of these become true at once:
//
//   1. requests for it are dispatched
//   2. it appears in the device's capabilities
//   3. whatever is driving the device can discover it
//
// Keep those in separate lists and they drift: the device claims something
// it cannot do, or quietly does something it never mentioned. This is the
// mechanism behind invariant 3, capabilities match the implementation.
//
// When an operation is unusable on this unit
// ------------------------------------------
// The entry stays, with available set to false and a reason attached. It is
// not removed. A caller can then distinguish "this device has no speaker"
// from "this firmware is too old to know about audio" — and the shape of
// the response never changes with the hardware, which is far easier to
// write against.

namespace stackchan::app {

// Description of one argument, as advertised in the capabilities.
//
// The permitted values are listed so that a caller can learn what is
// acceptable by asking, instead of reading prose and guessing.
struct ParamSpec {
  std::string_view name;
  // The permitted values, or nullptr when any string is accepted.
  const std::string_view* choices = nullptr;
  std::size_t choice_count = 0;

  [[nodiscard]] bool accepts(std::string_view value) const noexcept;
};

struct CommandSpec {
  CommandSpec() = default;

  // Most parameters need only a name and the defaults. A constructor is
  // provided because partial brace initialisation trips
  // -Wmissing-field-initializers.
  explicit CommandSpec(std::string_view command_name) noexcept : name(command_name) {}

  std::string_view name;  // namespaced, like "face.set_expression"

  // True when the response acknowledges work that continues asynchronously.
  bool is_async = false;

  const ParamSpec* params = nullptr;
  std::size_t param_count = 0;

  // Whether this unit can do it, decided when the registry is assembled.
  // Conditions that change while running — the gateway going away, say —
  // are reported as unavailable at call time instead.
  bool available = true;
  std::string_view unavailable_reason;  // meaningful only when unavailable
};

// Read the arguments, write the payload, return a result.
//
// A plain function pointer, which cannot capture, so there are no lifetimes
// to manage. State arrives through the context pointer.
using CommandHandler = domain::ErrorCode (*)(void* context,
                                             const ports::ParamReader& params,
                                             domain::JsonWriter& payload);

class CommandRegistry {
 public:
  // The limit. There are around a dozen commands, so this is not tight.
  static constexpr std::size_t kMaxCommands = 32;

  CommandRegistry() = default;
  ~CommandRegistry() = default;

  CommandRegistry(const CommandRegistry&) = delete;
  CommandRegistry& operator=(const CommandRegistry&) = delete;
  CommandRegistry(CommandRegistry&&) = delete;
  CommandRegistry& operator=(CommandRegistry&&) = delete;

  // Register a command. False if the name is already taken or the registry
  // is full — never silently dropped.
  [[nodiscard]] bool add(const CommandSpec& spec, CommandHandler handler,
                         void* context = nullptr) noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  [[nodiscard]] bool contains(std::string_view name) const noexcept {
    return find(name) != nullptr;
  }

  // Dispatch. The payload receives whatever the handler wrote.
  //
  // An unregistered name gives unknown_command; one that this unit cannot
  // do gives unsupported. Neither writes anything to the payload.
  [[nodiscard]] domain::ErrorCode dispatch(std::string_view name,
                                           const ports::ParamReader& params,
                                           domain::JsonWriter& payload) const noexcept;

  // Write the capabilities, generated from what is registered. No JSON is
  // written by hand anywhere.
  //
  // Called from inside an object the caller has already opened; this writes
  // the "capabilities" key and its array.
  void write_capabilities(domain::JsonWriter& out) const noexcept;

 private:
  struct Entry {
    CommandSpec spec;
    CommandHandler handler = nullptr;
    void* context = nullptr;
  };

  [[nodiscard]] const Entry* find(std::string_view name) const noexcept;

  std::array<Entry, kMaxCommands> entries_{};
  std::size_t count_ = 0;
};

}  // namespace stackchan::app
