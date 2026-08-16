#include <unity.h>

#include <set>
#include <string>

#include "stackchan/domain/expression.hpp"

using stackchan::domain::Expression;
using stackchan::domain::kExpressionCount;
using stackchan::domain::kExpressionNames;
using stackchan::domain::parse_expression;
using stackchan::domain::to_string;

namespace {

void test_there_are_six_expressions() {
  // Fixes how many there are: adding one is a change the other end has to
  // know about.
  TEST_ASSERT_EQUAL_UINT32(6, kExpressionCount);
}

void test_names_line_up_with_the_enum() {
  // The point: if the table and the enumeration fall out of order, values
  // are silently swapped.
  TEST_ASSERT_EQUAL_STRING("neutral", std::string{to_string(Expression::neutral)}.c_str());
  TEST_ASSERT_EQUAL_STRING("happy", std::string{to_string(Expression::happy)}.c_str());
  TEST_ASSERT_EQUAL_STRING("sad", std::string{to_string(Expression::sad)}.c_str());
  TEST_ASSERT_EQUAL_STRING("doubt", std::string{to_string(Expression::doubt)}.c_str());
  TEST_ASSERT_EQUAL_STRING("sleepy", std::string{to_string(Expression::sleepy)}.c_str());
  TEST_ASSERT_EQUAL_STRING("angry", std::string{to_string(Expression::angry)}.c_str());
}

void test_every_name_is_distinct() {
  std::set<std::string> seen;
  for (std::size_t i = 0; i < kExpressionCount; ++i) {
    TEST_ASSERT_TRUE(seen.insert(std::string{kExpressionNames[i]}).second);
  }
}

void test_every_expression_round_trips() {
  for (std::size_t i = 0; i < kExpressionCount; ++i) {
    const auto expression = static_cast<Expression>(i);
    const auto parsed = parse_expression(to_string(expression));
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL(expression, *parsed);
  }
}

void test_an_unknown_name_is_rejected_not_rounded() {
  // Falling back to a default would let a misspelled expression pass as
  // neutral.
  TEST_ASSERT_FALSE(parse_expression("ecstatic").has_value());
  TEST_ASSERT_FALSE(parse_expression("").has_value());
  TEST_ASSERT_FALSE(parse_expression("neutral ").has_value());
  TEST_ASSERT_FALSE(parse_expression(" neutral").has_value());
}

void test_names_are_case_sensitive() {
  // No ambiguity about which spelling to send.
  TEST_ASSERT_FALSE(parse_expression("Happy").has_value());
  TEST_ASSERT_FALSE(parse_expression("HAPPY").has_value());
  TEST_ASSERT_TRUE(parse_expression("happy").has_value());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_there_are_six_expressions);
  RUN_TEST(test_names_line_up_with_the_enum);
  RUN_TEST(test_every_name_is_distinct);
  RUN_TEST(test_every_expression_round_trips);
  RUN_TEST(test_an_unknown_name_is_rejected_not_rounded);
  RUN_TEST(test_names_are_case_sensitive);
  return UNITY_END();
}
