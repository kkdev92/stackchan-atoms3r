#include <unity.h>

#include <array>
#include <string>

#include "stackchan/domain/json_scan.hpp"

using stackchan::domain::json_find_bool;
using stackchan::domain::json_find_integer;
using stackchan::domain::json_find_object;
using stackchan::domain::json_find_object_checked;
using stackchan::domain::json_find_raw_string;
using stackchan::domain::json_unescape;
using stackchan::domain::JsonMemberResult;

namespace {

// The envelope itself, in the shape it actually arrives in.
constexpr const char* kEnvelope =
    R"({"v":1,"kind":"event","name":"reply.audio",)"
    R"("payload":{"seq":3,"text":"[happy]やあ。","rate":16000,)"
    R"("pcm":"TWFu","last":false}})";

void test_finds_the_top_level_fields_of_an_envelope() {
  std::int64_t version = 0;
  TEST_ASSERT_TRUE(json_find_integer(kEnvelope, "v", version));
  TEST_ASSERT_EQUAL_INT64(1, version);

  std::string_view kind;
  TEST_ASSERT_TRUE(json_find_raw_string(kEnvelope, "kind", kind));
  TEST_ASSERT_EQUAL_STRING("event", std::string{kind}.c_str());

  std::string_view payload;
  TEST_ASSERT_TRUE(json_find_object(kEnvelope, "payload", payload));
  TEST_ASSERT_EQUAL_CHAR('{', payload.front());
  TEST_ASSERT_EQUAL_CHAR('}', payload.back());
}

void test_reads_inside_the_extracted_payload() {
  std::string_view payload;
  TEST_ASSERT_TRUE(json_find_object(kEnvelope, "payload", payload));

  std::int64_t seq = -1;
  TEST_ASSERT_TRUE(json_find_integer(payload, "seq", seq));
  TEST_ASSERT_EQUAL_INT64(3, seq);

  std::int64_t rate = 0;
  TEST_ASSERT_TRUE(json_find_integer(payload, "rate", rate));
  TEST_ASSERT_EQUAL_INT64(16000, rate);

  bool last = true;
  TEST_ASSERT_TRUE(json_find_bool(payload, "last", last));
  TEST_ASSERT_FALSE(last);

  std::string_view pcm;
  TEST_ASSERT_TRUE(json_find_raw_string(payload, "pcm", pcm));
  TEST_ASSERT_EQUAL_STRING("TWFu", std::string{pcm}.c_str());
}

void test_a_nested_key_does_not_shadow_the_outer_level() {
  // A key inside the payload is not picked up by a search at the outer
  // level.
  const char* json = R"({"payload":{"seq":99},"seq":7})";
  std::int64_t seq = -1;
  TEST_ASSERT_TRUE(json_find_integer(json, "seq", seq));
  TEST_ASSERT_EQUAL_INT64(7, seq);
}

void test_a_key_only_in_the_nest_is_not_found_outside() {
  const char* json = R"({"payload":{"secret":1}})";
  std::int64_t value = -1;
  TEST_ASSERT_FALSE(json_find_integer(json, "secret", value));
}

void test_braces_and_quotes_inside_strings_do_not_confuse_the_walk() {
  // Braces and escaped quotes inside a string do not derail the scan.
  const char* json = R"({"text":"a\"}b{","x":5})";
  std::int64_t x = 0;
  TEST_ASSERT_TRUE(json_find_integer(json, "x", x));
  TEST_ASSERT_EQUAL_INT64(5, x);

  std::string_view raw;
  TEST_ASSERT_TRUE(json_find_raw_string(json, "text", raw));
  TEST_ASSERT_EQUAL_STRING("a\\\"}b{", std::string{raw}.c_str());
}

void test_arrays_are_skipped_correctly() {
  const char* json = R"({"arr":[{"x":1},"s\"]t",[2,3]],"y":true})";
  bool y = false;
  TEST_ASSERT_TRUE(json_find_bool(json, "y", y));
  TEST_ASSERT_TRUE(y);
}

void test_whitespace_between_tokens_is_tolerated() {
  const char* json = "{ \"a\" : 1 ,\n\t\"b\" : \"x\" }";
  std::int64_t a = 0;
  TEST_ASSERT_TRUE(json_find_integer(json, "a", a));
  TEST_ASSERT_EQUAL_INT64(1, a);
  std::string_view b;
  TEST_ASSERT_TRUE(json_find_raw_string(json, "b", b));
  TEST_ASSERT_EQUAL_STRING("x", std::string{b}.c_str());
}

void test_negative_integers_parse() {
  std::int64_t v = 0;
  TEST_ASSERT_TRUE(json_find_integer(R"({"n":-42})", "n", v));
  TEST_ASSERT_EQUAL_INT64(-42, v);
}

void test_floats_are_rejected_as_integers() {
  // The envelope never carries fractions, and one is not silently rounded.
  std::int64_t v = 0;
  TEST_ASSERT_FALSE(json_find_integer(R"({"n":1.5})", "n", v));
  TEST_ASSERT_FALSE(json_find_integer(R"({"n":1e3})", "n", v));
}

void test_type_mismatches_are_rejected() {
  std::int64_t v = 0;
  TEST_ASSERT_FALSE(json_find_integer(R"({"n":"7"})", "n", v));
  bool b = false;
  TEST_ASSERT_FALSE(json_find_bool(R"({"b":null})", "b", b));
  std::string_view s;
  TEST_ASSERT_FALSE(json_find_raw_string(R"({"s":7})", "s", s));
  std::string_view o;
  TEST_ASSERT_FALSE(json_find_object(R"({"o":[1]})", "o", o));
}

void test_checked_object_member_distinguishes_missing_and_invalid() {
  std::string_view payload;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::found),
      static_cast<int>(json_find_object_checked(
          R"({"name":"x","payload":{"text":"hi"},"v":1})", "payload",
          payload)));
  TEST_ASSERT_EQUAL_STRING(R"({"text":"hi"})", std::string{payload}.c_str());

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::missing),
      static_cast<int>(
          json_find_object_checked(R"({"name":"x"})", "payload", payload)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(
          R"({"payload":null})", "payload", payload)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(
          R"({"broken":,"payload":{}})", "payload", payload)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(
          R"({"payload":{}} trailing)", "payload", payload)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(
          R"({"payload":{},"payload":{}})", "payload", payload)));
}

void test_checked_object_member_validates_the_complete_document() {
  std::string_view payload;
  const std::string_view invalid_documents[] = {
      R"({,"payload":{}})",
      R"({"x":1,,"payload":{}})",
      R"({"payload":{},})",
      R"({"x":1 "payload":{}})",
      R"({"x":{],"payload":{}})",
      R"({"x":[},"payload":{}})",
      R"({"x":01})",
      R"({"x":"\q"})",
      "{\"x\":\"line\nbreak\"}",
      R"({"payload":{},"after":{"x":]}})",
      R"({"x":1,"x":2})",
      R"({"payload":{"text":"first","text":"second"}})",
  };
  for (const std::string_view document : invalid_documents) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(JsonMemberResult::invalid),
        static_cast<int>(
            json_find_object_checked(document, "payload", payload)));
  }

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::missing),
      static_cast<int>(json_find_object_checked(
          R"({"values":[0,-1,1.5,1e3,true,false,null,{"s":"line\n","u":"\u3042"}]})",
          "payload", payload)));

  std::string too_deep = R"({"x":)";
  too_deep.append(17, '[');
  too_deep += '0';
  too_deep.append(17, ']');
  too_deep += '}';
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(
          json_find_object_checked(too_deep, "payload", payload)));

  std::string nested{"0"};
  for (std::size_t depth = 0; depth < 16; ++depth) {
    nested = std::string{R"({"a":)"} + nested + R"(,"b":0,"c":0})";
  }
  const std::string nested_document =
      std::string{R"({"value":)"} + nested + '}';
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::missing),
      static_cast<int>(
          json_find_object_checked(nested_document, "payload", payload)));
}

void test_malformed_input_fails_without_crashing() {
  std::int64_t v = 0;
  TEST_ASSERT_FALSE(json_find_integer("", "a", v));
  TEST_ASSERT_FALSE(json_find_integer("not json", "a", v));
  TEST_ASSERT_FALSE(json_find_integer(R"({"a" 1})", "a", v));       // no colon
  TEST_ASSERT_FALSE(json_find_integer(R"({"a":1)", "b", v));        // never closed
  TEST_ASSERT_FALSE(json_find_integer(R"({"a":"unterminated)", "a", v));
  TEST_ASSERT_FALSE(json_find_integer("[1,2]", "a", v));            // an array
}

// ---------------------------------------------------------------- unescape

void test_unescape_passes_utf8_through() {
  std::array<char, 64> out{};
  const std::size_t len = json_unescape("こんにちは", out.data(), out.size());
  TEST_ASSERT_EQUAL_UINT32(15, len);  // five characters of three bytes
  TEST_ASSERT_EQUAL_MEMORY("こんにちは", out.data(), len);
}

void test_unescape_handles_the_contract_escapes() {
  std::array<char, 64> out{};
  const std::size_t len =
      json_unescape(R"(a\"b\\c\/d\ne\rf\tg\bh\fi)", out.data(), out.size());
  const char expected[] = "a\"b\\c/d\ne\rf\tg\bh\fi";
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected) - 1, len);
  TEST_ASSERT_EQUAL_MEMORY(expected, out.data(), len);
}

void test_unescape_rejects_unicode_escapes() {
  // The contract does not permit \uXXXX. Rather than quietly mangling it,
  // the read fails. (The backslash is added at runtime so the test's own
  // helpers do not consume it.)
  std::array<char, 64> out{};
  std::string raw = "a";
  raw += '\\';
  raw += "u3042b";
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, json_unescape(raw, out.data(), out.size()));
}

void test_unescape_rejects_a_trailing_backslash() {
  std::array<char, 64> out{};
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, json_unescape("abc\\", out.data(), out.size()));
}

void test_unescape_rejects_insufficient_capacity() {
  std::array<char, 4> out{};
  TEST_ASSERT_EQUAL_UINT32(SIZE_MAX, json_unescape("12345", out.data(), out.size()));
}

// A document whose "a" member is `levels` objects deep, with an integer at the
// bottom so every level is itself well formed.
std::string document_with_nesting(int levels) {
  std::string s = R"({"a":)";
  for (int i = 0; i < levels; ++i) {
    s += R"({"b":)";
  }
  s += "1";
  for (int i = 0; i < levels; ++i) {
    s += "}";
  }
  s += "}";
  return s;
}

// The scanner is recursive, so its stack cost is set by how deeply nested the
// input is -- and the input arrives from the network. The depth limit is what
// keeps that cost bounded, and this is the test that the suppression of
// misc-no-recursion in json_scan.cpp names, so that the suppression points at
// something specific rather than at a suite.
//
// Both sides of the boundary were already covered incidentally by
// test_checked_object_member_validates_the_complete_document -- a 17-deep array
// is rejected there, and a 16-deep document validates. Checked by moving the
// limit: tightening it to 15 fails that test, and removing it fails it too. So
// what this adds is narrower than it looks: the boundary as its own subject, the
// object path rather than the array path at the first rejected depth, and a
// depth far past the limit.
void test_nesting_is_bounded_and_the_boundary_is_where_it_should_be() {
  std::string_view out;

  const std::string at_the_limit = document_with_nesting(16);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::found),
      static_cast<int>(json_find_object_checked(at_the_limit, "a", out)));

  // One past, through the object path rather than the array path.
  const std::string one_past = document_with_nesting(17);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(one_past, "a", out)));

  // Far past it, so the refusal is not something that only holds for exactly
  // one level over. Rejection has to read as invalid: a caller told `missing`
  // would treat a document it could not validate as one that merely lacked the
  // member.
  const std::string far_past = document_with_nesting(400);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(JsonMemberResult::invalid),
      static_cast<int>(json_find_object_checked(far_past, "a", out)));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_finds_the_top_level_fields_of_an_envelope);
  RUN_TEST(test_reads_inside_the_extracted_payload);
  RUN_TEST(test_a_nested_key_does_not_shadow_the_outer_level);
  RUN_TEST(test_a_key_only_in_the_nest_is_not_found_outside);
  RUN_TEST(test_braces_and_quotes_inside_strings_do_not_confuse_the_walk);
  RUN_TEST(test_arrays_are_skipped_correctly);
  RUN_TEST(test_whitespace_between_tokens_is_tolerated);
  RUN_TEST(test_negative_integers_parse);
  RUN_TEST(test_floats_are_rejected_as_integers);
  RUN_TEST(test_type_mismatches_are_rejected);
  RUN_TEST(test_checked_object_member_distinguishes_missing_and_invalid);
  RUN_TEST(test_checked_object_member_validates_the_complete_document);
  RUN_TEST(test_nesting_is_bounded_and_the_boundary_is_where_it_should_be);
  RUN_TEST(test_malformed_input_fails_without_crashing);
  RUN_TEST(test_unescape_passes_utf8_through);
  RUN_TEST(test_unescape_handles_the_contract_escapes);
  RUN_TEST(test_unescape_rejects_unicode_escapes);
  RUN_TEST(test_unescape_rejects_a_trailing_backslash);
  RUN_TEST(test_unescape_rejects_insufficient_capacity);
  return UNITY_END();
}
