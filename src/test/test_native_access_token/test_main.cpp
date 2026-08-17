#include <unity.h>

#include <string>

#include "stackchan/domain/access_token.hpp"

using stackchan::domain::AccessToken;

namespace {

// Exactly the required length.
constexpr const char* kValid = "ABCDEFGH0123456789JKMNPQRSTVWXYZ";

void test_an_unset_token_matches_nothing() {
  // The point: a device that failed to generate a token must not end up
  // accepting an empty one.
  const AccessToken token = AccessToken::unset();
  TEST_ASSERT_FALSE(token.is_set());
  TEST_ASSERT_FALSE(token.matches(""));
  TEST_ASSERT_FALSE(token.matches(kValid));
  TEST_ASSERT_FALSE(token.matches("00000000000000000000000000000000"));
}

void test_a_token_matches_itself() {
  const AccessToken token = AccessToken::from_text(kValid);
  TEST_ASSERT_TRUE(token.is_set());
  TEST_ASSERT_TRUE(token.matches(kValid));
  TEST_ASSERT_EQUAL_STRING(kValid, std::string{token.text()}.c_str());
}

void test_a_different_token_does_not_match() {
  const AccessToken token = AccessToken::from_text(kValid);
  // Differing only in the last character.
  TEST_ASSERT_FALSE(token.matches("ABCDEFGH0123456789JKMNPQRSTVWXY0"));
  // And only in the first.
  TEST_ASSERT_FALSE(token.matches("0BCDEFGH0123456789JKMNPQRSTVWXYZ"));
}

void test_the_wrong_length_is_rejected() {
  const AccessToken token = AccessToken::from_text(kValid);
  TEST_ASSERT_FALSE(token.matches(""));
  TEST_ASSERT_FALSE(token.matches("ABCDEFGH"));
  // A correct prefix is not enough.
  TEST_ASSERT_FALSE(token.matches("ABCDEFGH0123456789JKMNPQRSTVWXY"));
  // Nor is anything longer.
  TEST_ASSERT_FALSE(token.matches("ABCDEFGH0123456789JKMNPQRSTVWXYZZ"));
}

void test_building_from_the_wrong_length_yields_unset() {
  // Someone who supplied a short value must not think it was accepted.
  TEST_ASSERT_FALSE(AccessToken::from_text("").is_set());
  TEST_ASSERT_FALSE(AccessToken::from_text("short").is_set());
  TEST_ASSERT_FALSE(AccessToken::from_text(std::string(31, 'A')).is_set());
  TEST_ASSERT_FALSE(AccessToken::from_text(std::string(33, 'A')).is_set());
  TEST_ASSERT_TRUE(AccessToken::from_text(std::string(32, 'A')).is_set());
}

void test_comparison_is_case_sensitive() {
  const AccessToken token = AccessToken::from_text(kValid);
  TEST_ASSERT_FALSE(token.matches("abcdefgh0123456789jkmnpqrstvwxyz"));
}

void test_an_empty_presented_value_does_not_crash() {
  // An empty candidate does not read past the end of anything. The
  // comparison always runs the full length, so this exercises that path.
  const AccessToken token = AccessToken::from_text(std::string(32, '0'));
  TEST_ASSERT_FALSE(token.matches(""));
}

void test_a_repeated_prefix_does_not_match() {
  // Because a shorter candidate is read with the position wrapping, this
  // confirms a repeated character cannot match by coincidence.
  const AccessToken token = AccessToken::from_text(std::string(32, 'A'));
  TEST_ASSERT_FALSE(token.matches("A"));
  TEST_ASSERT_FALSE(token.matches("AAAA"));
  TEST_ASSERT_TRUE(token.matches(std::string(32, 'A')));
}

void test_tokens_can_be_copied() {
  const AccessToken token = AccessToken::from_text(kValid);
  const AccessToken copy = token;
  TEST_ASSERT_TRUE(copy.matches(kValid));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_unset_token_matches_nothing);
  RUN_TEST(test_a_token_matches_itself);
  RUN_TEST(test_a_different_token_does_not_match);
  RUN_TEST(test_the_wrong_length_is_rejected);
  RUN_TEST(test_building_from_the_wrong_length_yields_unset);
  RUN_TEST(test_comparison_is_case_sensitive);
  RUN_TEST(test_an_empty_presented_value_does_not_crash);
  RUN_TEST(test_a_repeated_prefix_does_not_match);
  RUN_TEST(test_tokens_can_be_copied);
  return UNITY_END();
}
