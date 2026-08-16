#include "stackchan/domain/expression.hpp"

namespace stackchan::domain {

std::string_view to_string(Expression expression) noexcept {
  const auto index = static_cast<std::size_t>(expression);
  // The enumeration and the name table are kept in the same order. Out of
  // range returns the first name rather than falling back to neutral, since
  // the only way to get here is an invalid cast and hiding it would help
  // nobody.
  return index < kExpressionCount ? kExpressionNames[index] : kExpressionNames[0];
}

std::optional<Expression> parse_expression(std::string_view name) noexcept {
  for (std::size_t i = 0; i < kExpressionCount; ++i) {
    if (kExpressionNames[i] == name) {
      return static_cast<Expression>(i);
    }
  }
  return std::nullopt;
}

}  // namespace stackchan::domain
