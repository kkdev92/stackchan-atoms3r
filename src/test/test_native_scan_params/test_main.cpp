// ScanParams: a ParamReader over the scanner, rather than a JSON library.
// These fix that the meaning of a read is unchanged.

#include <unity.h>

#include <string_view>

#include "stackchan/app/scan_params.hpp"

using stackchan::app::ScanParams;

void setUp() {}
void tearDown() {}

void test_reads_a_string() {
  const ScanParams params{R"({"expression":"happy"})"};
  std::string_view value;
  TEST_ASSERT_TRUE(params.read_string("expression", value));
  TEST_ASSERT_EQUAL_STRING_LEN("happy", value.data(), value.size());
}

void test_reads_a_string_with_contract_escapes() {
  const ScanParams params{R"({"text":"line\nbreak \"quoted\""})"};
  std::string_view value;
  TEST_ASSERT_TRUE(params.read_string("text", value));
  TEST_ASSERT_EQUAL_STRING_LEN("line\nbreak \"quoted\"", value.data(), value.size());
}

void test_rejects_out_of_contract_escapes() {
  // The contract does not permit \uXXXX escapes, and a mangled string is
  // never returned in place of one. (Inside a raw string, so the compiler
  // does not interpret the escape itself.)
  const ScanParams params{"{\"text\":\"\\u3042\"}"};
  std::string_view value;
  TEST_ASSERT_FALSE(params.read_string("text", value));
}

void test_raw_utf8_passes_through() {
  // Raw UTF-8 is not an escape, so it passes through unchanged — which is
  // how the text actually arrives.
  const ScanParams params{"{\"text\":\"\xE3\x81\x82\"}"};  // one multi-byte character
  std::string_view value;
  TEST_ASSERT_TRUE(params.read_string("text", value));
  TEST_ASSERT_EQUAL_INT(3, static_cast<int>(value.size()));
}

void test_missing_key_is_not_found() {
  const ScanParams params{R"({"expression":"happy"})"};
  std::string_view value;
  TEST_ASSERT_FALSE(params.read_string("url", value));
}

void test_type_mismatch_is_not_found() {
  // A number is not readable as a string.
  const ScanParams params{R"({"volume":80,"loud":true})"};
  std::string_view text;
  TEST_ASSERT_FALSE(params.read_string("volume", text));
  std::int64_t number = 0;
  TEST_ASSERT_FALSE(params.read_integer("loud", number));
  bool flag = false;
  TEST_ASSERT_FALSE(params.read_bool("volume", flag));
}

void test_reads_integer_and_bool() {
  const ScanParams params{R"({"volume":80,"muted":false,"offset":-5})"};
  std::int64_t volume = 0;
  TEST_ASSERT_TRUE(params.read_integer("volume", volume));
  TEST_ASSERT_EQUAL_INT64(80, volume);
  std::int64_t offset = 0;
  TEST_ASSERT_TRUE(params.read_integer("offset", offset));
  TEST_ASSERT_EQUAL_INT64(-5, offset);
  bool muted = true;
  TEST_ASSERT_TRUE(params.read_bool("muted", muted));
  TEST_ASSERT_FALSE(muted);
}

void test_two_strings_share_the_arena() {
  const ScanParams params{R"({"ssid":"HomeWifi","pass":"secret\t1"})"};
  std::string_view ssid;
  std::string_view pass;
  TEST_ASSERT_TRUE(params.read_string("ssid", ssid));
  TEST_ASSERT_TRUE(params.read_string("pass", pass));
  // An earlier value is not clobbered by a later read; the arena advances
  // rather than reusing.
  TEST_ASSERT_EQUAL_STRING_LEN("HomeWifi", ssid.data(), ssid.size());
  TEST_ASSERT_EQUAL_STRING_LEN("secret\t1", pass.data(), pass.size());
}

void test_arena_exhaustion_fails_closed() {
  // A value large enough to exhaust the arena fails, rather than returning
  // what was unescaped so far.
  char body[700];
  std::size_t at = 0;
  const char head[] = R"({"big":")";
  for (const char c : std::string_view{head}) body[at++] = c;
  for (int i = 0; i < 600; ++i) body[at++] = 'x';
  body[at++] = '"';
  body[at++] = '}';
  const ScanParams params{std::string_view{body, at}};
  std::string_view value;
  TEST_ASSERT_FALSE(params.read_string("big", value));
}

void test_empty_payloads() {
  const ScanParams none{std::string_view{}};
  TEST_ASSERT_TRUE(none.empty());
  const ScanParams braces{"{}"};
  TEST_ASSERT_TRUE(braces.empty());
  const ScanParams spaced{"{  \n }"};
  TEST_ASSERT_TRUE(spaced.empty());
  const ScanParams filled{R"({"a":1})"};
  TEST_ASSERT_FALSE(filled.empty());
  const ScanParams malformed{"{broken}"};
  TEST_ASSERT_FALSE(malformed.empty());
}

void test_nested_object_keys_are_not_visible() {
  // A key inside a nested object is not at this level.
  const ScanParams params{R"({"inner":{"expression":"sad"}})"};
  std::string_view value;
  TEST_ASSERT_FALSE(params.read_string("expression", value));
  TEST_ASSERT_FALSE(params.empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_a_string);
  RUN_TEST(test_reads_a_string_with_contract_escapes);
  RUN_TEST(test_rejects_out_of_contract_escapes);
  RUN_TEST(test_raw_utf8_passes_through);
  RUN_TEST(test_missing_key_is_not_found);
  RUN_TEST(test_type_mismatch_is_not_found);
  RUN_TEST(test_reads_integer_and_bool);
  RUN_TEST(test_two_strings_share_the_arena);
  RUN_TEST(test_arena_exhaustion_fails_closed);
  RUN_TEST(test_empty_payloads);
  RUN_TEST(test_nested_object_keys_are_not_visible);
  return UNITY_END();
}
