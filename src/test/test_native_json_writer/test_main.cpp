#include <unity.h>

#include <array>
#include <string>

#include "stackchan/domain/json_writer.hpp"

using stackchan::domain::JsonWriter;

namespace {

// Take the result as a string, which makes the comparisons readable.
std::string written(const JsonWriter& writer) { return std::string{writer.text()}; }

void test_an_empty_object() {
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING("{}", written(w).c_str());
}

void test_an_empty_array() {
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_array();
  w.end_array();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING("[]", written(w).c_str());
}

void test_commas_go_between_members_not_before_the_first() {
  // Escaping is where hand-written serialisation usually breaks.
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("a", std::uint64_t{1});
  w.member("b", std::uint64_t{2});
  w.member("c", std::uint64_t{3});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"a":1,"b":2,"c":3})", written(w).c_str());
}

void test_commas_go_between_array_elements() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_array();
  w.value(std::string_view{"neutral"});
  w.value(std::string_view{"happy"});
  w.value(std::string_view{"sad"});
  w.end_array();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"(["neutral","happy","sad"])", written(w).c_str());
}

void test_nesting_objects_and_arrays() {
  // The shape of a capability list. If this can be built, the type is
  // useful.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("protocol", std::uint64_t{1});
  w.key("capabilities");
  w.begin_array();
  w.begin_object();
  w.member("name", std::string_view{"face.set_expression"});
  w.key("params");
  w.begin_array();
  w.value(std::string_view{"happy"});
  w.end_array();
  w.end_object();
  w.end_array();
  w.end_object();

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"protocol":1,"capabilities":[{"name":"face.set_expression","params":["happy"]}]})",
      written(w).c_str());
}

void test_empty_containers_nested_in_objects() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.key("a");
  w.begin_array();
  w.end_array();
  w.key("b");
  w.begin_object();
  w.end_object();
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"a":[],"b":{}})", written(w).c_str());
}

void test_booleans_and_null() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("available", true);
  w.member("estop", false);
  w.key("reason");
  w.null_value();
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"available":true,"estop":false,"reason":null})",
                           written(w).c_str());
}

void test_a_c_string_is_written_as_text_not_as_true() {
  // The trap where const char* binds to the bool overload: pointer-to-bool
  // is a standard conversion and beats the user-defined one to string_view.
  // The result is a field that should read "connected" quietly becoming
  // true.
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  const char* phase = "connected";
  w.begin_object();
  w.member("phase", phase);
  w.key("also");
  w.value(phase);
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"phase":"connected","also":"connected"})",
                           written(w).c_str());
}

void test_unsigned_numbers_including_the_extremes() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_array();
  w.value(std::uint64_t{0});
  w.value(std::uint64_t{1});
  w.value(std::uint64_t{18446744073709551615ull});
  w.end_array();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING("[0,1,18446744073709551615]", written(w).c_str());
}

void test_signed_numbers_including_the_minimum() {
  // Negating the most negative value overflows. This checks that path is
  // not taken.
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_array();
  w.value(std::int64_t{0});
  w.value(std::int64_t{-1});
  w.value(std::int64_t{-9223372036854775807LL - 1});
  w.end_array();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING("[0,-1,-9223372036854775808]", written(w).c_str());
}

void test_quotes_and_backslashes_are_escaped() {
  // A missed escape breaks the parser at the other end.
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("message", std::string_view{R"(say "hi" \ now)"});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"message":"say \"hi\" \\ now"})", written(w).c_str());
}

void test_newlines_and_tabs_are_escaped() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("text", std::string_view{"line1\nline2\tend\r"});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"text":"line1\nline2\tend\r"})", written(w).c_str());
}

void test_other_control_characters_become_unicode_escapes() {
  // Emitted raw, this would be invalid JSON.
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("text", std::string_view{"a\x01\x1F"});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"text":"a\u0001\u001f"})", written(w).c_str());
}

void test_utf8_passes_through_unchanged() {
  // Non-ASCII passes through as UTF-8. Expanding it to \u escapes would
  // sextuple the length and make the logs unreadable.
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("text", std::string_view{"こんにちは"});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"text":"こんにちは"})", written(w).c_str());
}

void test_keys_are_escaped_too() {
  std::array<char, 128> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member(std::string_view{R"(od"d)"}, std::uint64_t{1});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"od\"d":1})", written(w).c_str());
}

void test_overflow_is_reported_not_hidden() {
  // The point of the type. Output that does not fit is reported, not
  // silently truncated into something that parses wrong.
  std::array<char, 8> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("a_long_key", std::string_view{"a_long_value"});
  w.end_object();

  TEST_ASSERT_TRUE(w.overflowed());
  TEST_ASSERT_FALSE(w.valid());
}

void test_overflow_never_writes_past_the_buffer() {
  // A sentinel byte confirms nothing was written past the end.
  std::array<char, 16> buffer{};
  buffer.fill('#');
  JsonWriter w{buffer.data(), 8};  // only the first eight bytes are usable

  w.begin_object();
  for (int i = 0; i < 50; ++i) {
    w.member("key", std::string_view{"value"});
  }
  w.end_object();

  TEST_ASSERT_TRUE(w.overflowed());
  TEST_ASSERT_TRUE(w.length() <= 8);
  for (std::size_t i = 8; i < buffer.size(); ++i) {
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('#', buffer[i],
                                   "written past the end of the buffer");
  }
}

void test_required_tells_how_much_was_needed() {
  // Reports the size needed, so a caller knows what to allocate next time.
  std::array<char, 4> small{};
  JsonWriter w{small.data(), small.size()};
  w.begin_object();
  w.member("a", std::uint64_t{1});
  w.end_object();

  TEST_ASSERT_TRUE(w.overflowed());
  // {"a":1} is seven bytes.
  TEST_ASSERT_EQUAL_UINT32(7, w.required());

  std::array<char, 8> enough{};
  JsonWriter w2{enough.data(), enough.size()};
  w2.begin_object();
  w2.member("a", std::uint64_t{1});
  w2.end_object();
  TEST_ASSERT_TRUE(w2.valid());
  TEST_ASSERT_EQUAL_UINT32(7, w2.required());
  TEST_ASSERT_EQUAL_UINT32(7, w2.length());
}

void test_an_unclosed_object_is_not_valid() {
  // Something left unclosed must never be sent.
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("a", std::uint64_t{1});
  TEST_ASSERT_FALSE(w.valid());
  TEST_ASSERT_FALSE(w.overflowed());  // not an overflow, merely unclosed
}

void test_mismatched_brackets_are_caught() {
  // Closing an object with a square bracket.
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.end_array();
  TEST_ASSERT_FALSE(w.valid());
  TEST_ASSERT_TRUE(w.overflowed());
}

void test_closing_without_opening_is_caught() {
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.end_object();
  TEST_ASSERT_FALSE(w.valid());
  TEST_ASSERT_TRUE(w.overflowed());
}

void test_excessive_nesting_is_caught() {
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  for (std::size_t i = 0; i <= JsonWriter::kMaxDepth; ++i) {
    w.begin_array();
  }
  TEST_ASSERT_TRUE(w.overflowed());
}

void test_reset_allows_reuse() {
  // The same buffer can be reused rather than built afresh per response.
  std::array<char, 64> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("a", std::uint64_t{1});
  w.end_object();
  TEST_ASSERT_EQUAL_STRING(R"({"a":1})", written(w).c_str());

  w.reset();
  w.begin_object();
  w.member("b", std::uint64_t{2});
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(R"({"b":2})", written(w).c_str());
}

void test_reset_clears_an_overflow() {
  std::array<char, 4> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("long_key", std::string_view{"long_value"});
  w.end_object();
  TEST_ASSERT_TRUE(w.overflowed());

  w.reset();
  TEST_ASSERT_FALSE(w.overflowed());
  w.begin_object();
  w.end_object();
  TEST_ASSERT_TRUE(w.valid());
}

void test_an_error_result_looks_as_designed() {
  // The shape actually sent.
  std::array<char, 256> buffer{};
  JsonWriter w{buffer.data(), buffer.size()};
  w.begin_object();
  w.member("v", std::uint64_t{1});
  w.member("id", std::string_view{"01JZ8"});
  w.member("kind", std::string_view{"result"});
  w.member("ok", false);
  w.key("error");
  w.begin_object();
  w.member("code", std::string_view{"estop_engaged"});
  w.member("message", std::string_view{"emergency stop is engaged"});
  w.member("retryable", false);
  w.end_object();
  w.end_object();

  TEST_ASSERT_TRUE(w.valid());
  TEST_ASSERT_EQUAL_STRING(
      R"({"v":1,"id":"01JZ8","kind":"result","ok":false,)"
      R"("error":{"code":"estop_engaged","message":"emergency stop is engaged",)"
      R"("retryable":false}})",
      written(w).c_str());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_empty_object);
  RUN_TEST(test_an_empty_array);
  RUN_TEST(test_commas_go_between_members_not_before_the_first);
  RUN_TEST(test_commas_go_between_array_elements);
  RUN_TEST(test_nesting_objects_and_arrays);
  RUN_TEST(test_empty_containers_nested_in_objects);
  RUN_TEST(test_booleans_and_null);
  RUN_TEST(test_a_c_string_is_written_as_text_not_as_true);
  RUN_TEST(test_unsigned_numbers_including_the_extremes);
  RUN_TEST(test_signed_numbers_including_the_minimum);
  RUN_TEST(test_quotes_and_backslashes_are_escaped);
  RUN_TEST(test_newlines_and_tabs_are_escaped);
  RUN_TEST(test_other_control_characters_become_unicode_escapes);
  RUN_TEST(test_utf8_passes_through_unchanged);
  RUN_TEST(test_keys_are_escaped_too);
  RUN_TEST(test_overflow_is_reported_not_hidden);
  RUN_TEST(test_overflow_never_writes_past_the_buffer);
  RUN_TEST(test_required_tells_how_much_was_needed);
  RUN_TEST(test_an_unclosed_object_is_not_valid);
  RUN_TEST(test_mismatched_brackets_are_caught);
  RUN_TEST(test_closing_without_opening_is_caught);
  RUN_TEST(test_excessive_nesting_is_caught);
  RUN_TEST(test_reset_allows_reuse);
  RUN_TEST(test_reset_clears_an_overflow);
  RUN_TEST(test_an_error_result_looks_as_designed);
  return UNITY_END();
}
