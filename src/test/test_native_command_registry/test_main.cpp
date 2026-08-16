#include <unity.h>

#include <array>
#include <map>
#include <string>

#include "stackchan/app/command_registry.hpp"

using stackchan::app::CommandRegistry;
using stackchan::app::CommandSpec;
using stackchan::app::ParamSpec;
using stackchan::domain::ErrorCode;
using stackchan::domain::JsonWriter;

namespace {

// A parameter reader for the tests. On the device this is replaced by one
// backed by a real parser; the handler code is identical either way.
class FakeParams final : public stackchan::ports::ParamReader {
 public:
  // The setters have distinct names on purpose. With one overload set,
  // set(key, "happy") binds to bool — pointer-to-bool is a standard
  // conversion and beats the user-defined one to string_view.
  void set_string(std::string_view key, std::string_view value) {
    strings_[std::string{key}] = std::string{value};
  }
  void set_integer(std::string_view key, std::int64_t value) {
    integers_[std::string{key}] = value;
  }
  void set_bool(std::string_view key, bool value) { bools_[std::string{key}] = value; }

  bool read_string(std::string_view key, std::string_view& out) const override {
    const auto it = strings_.find(std::string{key});
    if (it == strings_.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
  bool read_integer(std::string_view key, std::int64_t& out) const override {
    const auto it = integers_.find(std::string{key});
    if (it == integers_.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
  bool read_bool(std::string_view key, bool& out) const override {
    const auto it = bools_.find(std::string{key});
    if (it == bools_.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
  bool empty() const override {
    return strings_.empty() && integers_.empty() && bools_.empty();
  }

 private:
  std::map<std::string, std::string> strings_;
  std::map<std::string, std::int64_t> integers_;
  std::map<std::string, bool> bools_;
};

constexpr std::string_view kExpressions[] = {"neutral", "happy", "sad",
                                            "doubt",   "sleepy", "angry"};
constexpr std::string_view kMotions[] = {"nod", "shake", "idle", "stop"};

// A handler that records having been called.
struct Spy {
  int calls = 0;
  std::string last_expression;

  static ErrorCode handle(void* context, const stackchan::ports::ParamReader& params,
                          JsonWriter& payload) {
    auto* self = static_cast<Spy*>(context);
    ++self->calls;
    std::string_view expression;
    if (params.read_string("expression", expression)) {
      self->last_expression = std::string{expression};
    }
    payload.begin_object();
    payload.member("applied", true);
    payload.end_object();
    return ErrorCode::none;
  }
};

ErrorCode ok_handler(void*, const stackchan::ports::ParamReader&, JsonWriter&) {
  return ErrorCode::none;
}

std::string capabilities_of(const CommandRegistry& registry) {
  std::array<char, 2048> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  registry.write_capabilities(w);
  w.end_object();
  TEST_ASSERT_TRUE_MESSAGE(w.valid(), "capabilities produced invalid JSON");
  return std::string{w.text()};
}

// --------------------------------------------------------- registration

void test_starts_empty() {
  const CommandRegistry registry;
  TEST_ASSERT_EQUAL_UINT32(0, registry.size());
  TEST_ASSERT_FALSE(registry.contains("face.set_expression"));
}

void test_adding_a_command_registers_it() {
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.set_expression"}, &ok_handler));
  TEST_ASSERT_EQUAL_UINT32(1, registry.size());
  TEST_ASSERT_TRUE(registry.contains("face.set_expression"));
}

void test_a_null_handler_is_rejected() {
  CommandRegistry registry;
  TEST_ASSERT_FALSE(registry.add(CommandSpec{"face.set_expression"}, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0, registry.size());
}

void test_an_empty_name_is_rejected() {
  CommandRegistry registry;
  TEST_ASSERT_FALSE(registry.add(CommandSpec{""}, &ok_handler));
  TEST_ASSERT_EQUAL_UINT32(0, registry.size());
}

void test_a_duplicate_name_is_rejected() {
  // Registering a name twice would leave it ambiguous which handler runs.
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.set_expression"}, &ok_handler));
  TEST_ASSERT_FALSE(registry.add(CommandSpec{"face.set_expression"}, &ok_handler));
  TEST_ASSERT_EQUAL_UINT32(1, registry.size());
}

void test_registering_beyond_the_limit_is_rejected() {
  CommandRegistry registry;
  std::array<std::string, CommandRegistry::kMaxCommands + 1> names{};
  for (std::size_t i = 0; i < names.size(); ++i) {
    names[i] = "cmd." + std::to_string(i);
  }
  for (std::size_t i = 0; i < CommandRegistry::kMaxCommands; ++i) {
    TEST_ASSERT_TRUE(registry.add(CommandSpec{names[i]}, &ok_handler));
  }
  // Full means false, never a silent drop.
  TEST_ASSERT_FALSE(registry.add(CommandSpec{names.back()}, &ok_handler));
  TEST_ASSERT_EQUAL_UINT32(CommandRegistry::kMaxCommands, registry.size());
}

// ------------------------------------------------------------ dispatch

void test_dispatch_calls_the_registered_handler() {
  CommandRegistry registry;
  Spy spy;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.set_expression"}, &Spy::handle, &spy));

  FakeParams params;
  params.set_string("expression", "happy");
  std::array<char, 128> buffer{};
  JsonWriter payload{buffer.data(), buffer.size()};

  TEST_ASSERT_EQUAL(ErrorCode::none,
                    registry.dispatch("face.set_expression", params, payload));
  TEST_ASSERT_EQUAL_INT(1, spy.calls);
  TEST_ASSERT_EQUAL_STRING("happy", spy.last_expression.c_str());
  TEST_ASSERT_TRUE(payload.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"applied":true})", std::string{payload.text()}.c_str());
}

void test_dispatching_an_unknown_name_says_so() {
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.set_expression"}, &ok_handler));

  FakeParams params;
  std::array<char, 128> buffer{};
  JsonWriter payload{buffer.data(), buffer.size()};

  TEST_ASSERT_EQUAL(ErrorCode::unknown_command,
                    registry.dispatch("face.no_such_thing", params, payload));
  // Nothing is written; the caller builds the error response.
  TEST_ASSERT_EQUAL_UINT32(0, payload.length());
}

void test_an_unavailable_command_reports_unsupported_not_unknown() {
  // "Not registered" and "unusable on this unit" are different answers,
  // because they change whether the caller should try again.
  CommandRegistry registry;
  CommandSpec spec{"motion.perform"};
  spec.available = false;
  spec.unavailable_reason = "servo_not_attached";
  Spy spy;
  TEST_ASSERT_TRUE(registry.add(spec, &Spy::handle, &spy));

  FakeParams params;
  std::array<char, 128> buffer{};
  JsonWriter payload{buffer.data(), buffer.size()};

  TEST_ASSERT_EQUAL(ErrorCode::unsupported,
                    registry.dispatch("motion.perform", params, payload));
  // The handler is not called. An unusable command must not run.
  TEST_ASSERT_EQUAL_INT(0, spy.calls);
}

void test_the_handler_error_is_passed_through() {
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(
      CommandSpec{"conversation.start"},
      [](void*, const stackchan::ports::ParamReader&, JsonWriter&) {
        return ErrorCode::estop_engaged;
      }));

  FakeParams params;
  std::array<char, 128> buffer{};
  JsonWriter payload{buffer.data(), buffer.size()};
  TEST_ASSERT_EQUAL(ErrorCode::estop_engaged,
                    registry.dispatch("conversation.start", params, payload));
}

void test_each_command_reaches_its_own_handler() {
  CommandRegistry registry;
  Spy face;
  Spy motion;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.set_expression"}, &Spy::handle, &face));
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"motion.perform"}, &Spy::handle, &motion));

  FakeParams params;
  std::array<char, 128> buffer{};
  JsonWriter payload{buffer.data(), buffer.size()};
  (void)registry.dispatch("motion.perform", params, payload);

  TEST_ASSERT_EQUAL_INT(0, face.calls);
  TEST_ASSERT_EQUAL_INT(1, motion.calls);
}

// -------------------------------------------------- parameter descriptions

void test_a_param_with_choices_accepts_only_those() {
  ParamSpec spec{"expression", kExpressions, 6};
  TEST_ASSERT_TRUE(spec.accepts("happy"));
  TEST_ASSERT_TRUE(spec.accepts("neutral"));
  TEST_ASSERT_FALSE(spec.accepts("ecstatic"));
  TEST_ASSERT_FALSE(spec.accepts(""));
  // Case is significant, so there is no ambiguity about what to send.
  TEST_ASSERT_FALSE(spec.accepts("Happy"));
}

void test_a_param_without_choices_accepts_anything() {
  const ParamSpec spec{"text"};
  TEST_ASSERT_TRUE(spec.accepts("anything at all"));
  TEST_ASSERT_TRUE(spec.accepts(""));
}

// ---------------------------------------------------------------- capabilities

void test_capabilities_of_an_empty_registry_is_an_empty_array() {
  const CommandRegistry registry;
  TEST_ASSERT_EQUAL_STRING(R"({"capabilities":[]})", capabilities_of(registry).c_str());
}

void test_capabilities_lists_what_was_registered() {
  // The point of the type: no hand-written JSON, generated entirely from
  // what was registered.
  CommandRegistry registry;
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"device.describe"}, &ok_handler));
  TEST_ASSERT_TRUE(registry.add(CommandSpec{"device.state"}, &ok_handler));

  TEST_ASSERT_EQUAL_STRING(
      R"({"capabilities":[{"name":"device.describe"},{"name":"device.state"}]})",
      capabilities_of(registry).c_str());
}

void test_adding_a_command_makes_it_appear_in_capabilities() {
  // Confirms there is no room to disagree: registration is the only entry
  // point.
  CommandRegistry registry;
  const std::string before = capabilities_of(registry);
  TEST_ASSERT_TRUE(before.find("face.show_message") == std::string::npos);

  TEST_ASSERT_TRUE(registry.add(CommandSpec{"face.show_message"}, &ok_handler));
  const std::string after = capabilities_of(registry);
  TEST_ASSERT_TRUE(after.find("face.show_message") != std::string::npos);
}

void test_capabilities_marks_asynchronous_commands() {
  CommandRegistry registry;
  CommandSpec spec{"audio.record"};
  spec.is_async = true;
  TEST_ASSERT_TRUE(registry.add(spec, &ok_handler));

  TEST_ASSERT_EQUAL_STRING(R"({"capabilities":[{"name":"audio.record","async":true}]})",
                           capabilities_of(registry).c_str());
}

void test_capabilities_lists_the_accepted_values() {
  // A caller can learn the permitted values by asking, instead of reading
  // prose and guessing.
  CommandRegistry registry;
  static constexpr ParamSpec kParams[] = {{"expression", kExpressions, 6}};
  CommandSpec spec{"face.set_expression"};
  spec.params = kParams;
  spec.param_count = 1;
  TEST_ASSERT_TRUE(registry.add(spec, &ok_handler));

  TEST_ASSERT_EQUAL_STRING(
      R"({"capabilities":[{"name":"face.set_expression","params":{"expression":)"
      R"(["neutral","happy","sad","doubt","sleepy","angry"]}}]})",
      capabilities_of(registry).c_str());
}

void test_capabilities_shows_a_free_form_param_as_a_string() {
  CommandRegistry registry;
  static constexpr ParamSpec kParams[] = {{"text"}};
  CommandSpec spec{"face.show_message"};
  spec.params = kParams;
  spec.param_count = 1;
  TEST_ASSERT_TRUE(registry.add(spec, &ok_handler));

  TEST_ASSERT_EQUAL_STRING(
      R"({"capabilities":[{"name":"face.show_message","params":{"text":"string"}}]})",
      capabilities_of(registry).c_str());
}

void test_an_unavailable_command_keeps_its_entry_with_a_reason() {
  // An unusable command keeps its entry and reports a reason, so the shape
  // of the response does not change with the hardware.
  CommandRegistry registry;
  static constexpr ParamSpec kParams[] = {{"motion", kMotions, 4}};
  CommandSpec spec{"motion.perform"};
  spec.params = kParams;
  spec.param_count = 1;
  spec.available = false;
  spec.unavailable_reason = "servo_not_attached";
  TEST_ASSERT_TRUE(registry.add(spec, &ok_handler));

  TEST_ASSERT_EQUAL_STRING(
      R"({"capabilities":[{"name":"motion.perform","params":{"motion":)"
      R"(["nod","shake","idle","stop"]},"available":false,)"
      R"("reason":"servo_not_attached"}]})",
      capabilities_of(registry).c_str());
}

void test_capabilities_stays_valid_json_with_many_commands() {
  // Commas and brackets stay correct as the list grows.
  CommandRegistry registry;
  std::array<std::string, 20> names{};
  for (std::size_t i = 0; i < names.size(); ++i) {
    names[i] = "cmd.n" + std::to_string(i);
    CommandSpec spec{names[i]};
    spec.is_async = (i % 2 == 0);
    TEST_ASSERT_TRUE(registry.add(spec, &ok_handler));
  }
  const std::string text = capabilities_of(registry);
  // capabilities_of checks valid() internally; this checks the contents.
  TEST_ASSERT_TRUE(text.find("cmd.n0") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("cmd.n19") != std::string::npos);
  TEST_ASSERT_TRUE(text.find(",,") == std::string::npos);
  TEST_ASSERT_TRUE(text.find("[,") == std::string::npos);
}

void test_capabilities_reports_overflow_rather_than_truncating() {
  // With too small a buffer, partially written JSON is not reported as a
  // success.
  CommandRegistry registry;
  std::array<std::string, 10> names{};
  for (std::size_t i = 0; i < names.size(); ++i) {
    names[i] = "command.with.a.fairly.long.name." + std::to_string(i);
    TEST_ASSERT_TRUE(registry.add(CommandSpec{names[i]}, &ok_handler));
  }

  std::array<char, 32> tiny{};
  JsonWriter w{tiny.data(), tiny.size()};
  w.begin_object();
  registry.write_capabilities(w);
  w.end_object();

  TEST_ASSERT_TRUE(w.overflowed());
  TEST_ASSERT_FALSE(w.valid());
  // The size needed is still reported.
  TEST_ASSERT_TRUE(w.required() > tiny.size());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_adding_a_command_registers_it);
  RUN_TEST(test_a_null_handler_is_rejected);
  RUN_TEST(test_an_empty_name_is_rejected);
  RUN_TEST(test_a_duplicate_name_is_rejected);
  RUN_TEST(test_registering_beyond_the_limit_is_rejected);
  RUN_TEST(test_dispatch_calls_the_registered_handler);
  RUN_TEST(test_dispatching_an_unknown_name_says_so);
  RUN_TEST(test_an_unavailable_command_reports_unsupported_not_unknown);
  RUN_TEST(test_the_handler_error_is_passed_through);
  RUN_TEST(test_each_command_reaches_its_own_handler);
  RUN_TEST(test_a_param_with_choices_accepts_only_those);
  RUN_TEST(test_a_param_without_choices_accepts_anything);
  RUN_TEST(test_capabilities_of_an_empty_registry_is_an_empty_array);
  RUN_TEST(test_capabilities_lists_what_was_registered);
  RUN_TEST(test_adding_a_command_makes_it_appear_in_capabilities);
  RUN_TEST(test_capabilities_marks_asynchronous_commands);
  RUN_TEST(test_capabilities_lists_the_accepted_values);
  RUN_TEST(test_capabilities_shows_a_free_form_param_as_a_string);
  RUN_TEST(test_an_unavailable_command_keeps_its_entry_with_a_reason);
  RUN_TEST(test_capabilities_stays_valid_json_with_many_commands);
  RUN_TEST(test_capabilities_reports_overflow_rather_than_truncating);
  return UNITY_END();
}
