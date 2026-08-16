#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The expressions the face can wear.
//
// The set is part of the contract: names come in from outside, so adding
// one here without saying so leaves the two ends disagreeing. How each is
// drawn is not decided here — that is the display's business.
//
// Why the name table is public
// ----------------------------
// So that the list advertised in the device's capabilities and the check
// applied to an incoming value read the same table. Keep two copies and
// eventually the device advertises something it then rejects. This is part
// of invariant 3: capabilities match the implementation.

namespace stackchan::domain {

enum class Expression : std::uint8_t {
  neutral,
  happy,
  sad,
  doubt,
  sleepy,
  angry,
};

// The names used on the wire, in the same order as the enumeration, ready
// to be advertised as the permitted values.
inline constexpr std::array<std::string_view, 6> kExpressionNames = {
    "neutral", "happy", "sad", "doubt", "sleepy", "angry",
};
inline constexpr std::size_t kExpressionCount = kExpressionNames.size();

[[nodiscard]] std::string_view to_string(Expression expression) noexcept;

// Parse a name, or nullopt if it is not one of them.
//
// Deliberately no fallback to a default: a misspelled expression should be
// an error, not silently neutral.
[[nodiscard]] std::optional<Expression> parse_expression(std::string_view name) noexcept;

}  // namespace stackchan::domain
