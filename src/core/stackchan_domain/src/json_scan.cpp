#include "stackchan/domain/json_scan.hpp"

namespace stackchan::domain {
namespace {

// What kind of value was found. Scanning only identifies the kind; making
// sense of the contents is the caller's job.
enum class ValueKind : std::uint8_t { string, number, boolean, null, object, array };

[[nodiscard]] bool is_ws(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Skip the string literal at i, which must be its opening quote. On
// success i lands just past the closing quote and out holds the contents.
[[nodiscard]] bool skip_string(std::string_view s, std::size_t& i,
                               std::string_view& out_raw) noexcept {
  const std::size_t start = i + 1;
  ++i;
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\\') {
  // The character after a backslash is skipped unconditionally, so an
      // escaped quote is not mistaken for the end of the string.
      i += 2;
      continue;
    }
    if (c == '"') {
      out_raw = s.substr(start, i - start);
      ++i;
      return true;
    }
    ++i;
  }
  return false;  // no closing quote
}

void eat_ws(std::string_view s, std::size_t& i) noexcept {
  while (i < s.size() && is_ws(s[i])) {
    ++i;
  }
}

// Skip from a '{' or '[' to its match. Brackets inside strings do not count.
[[nodiscard]] bool skip_container(std::string_view s, std::size_t& i,
                                  std::string_view& out_span) noexcept {
  const std::size_t start = i;
  std::size_t depth = 0;
  while (i < s.size()) {
    const char b = s[i];
    if (b == '"') {
      std::string_view ignored;
      if (!skip_string(s, i, ignored)) {
        return false;
      }
      continue;
    }
    if (b == '{' || b == '[') {
      ++depth;
    } else if (b == '}' || b == ']') {
      --depth;
      if (depth == 0) {
        ++i;
        out_span = s.substr(start, i - start);
        return true;
      }
    }
    ++i;
  }
  return false;
}

[[nodiscard]] bool skip_number(std::string_view s, std::size_t& i,
                               std::string_view& out_span) noexcept {
  const std::size_t start = i;
  ++i;
  while (i < s.size()) {
    const char d = s[i];
    const bool number_char = (d >= '0' && d <= '9') || d == '.' || d == 'e' ||
                             d == 'E' || d == '+' || d == '-';
    if (!number_char) {
      break;
    }
    ++i;
  }
  out_span = s.substr(start, i - start);
  return true;
}

// The bare word a value may begin with, chosen by its first letter. Any
// other letter yields "null", which then fails to match and is rejected by
// the caller, so no letter has to be excluded here.
[[nodiscard]] std::string_view keyword_starting_with(char c) noexcept {
  if (c == 't') {
    return "true";
  }
  if (c == 'f') {
    return "false";
  }
  return "null";
}

[[nodiscard]] bool skip_keyword(std::string_view s, std::size_t& i,
                                std::string_view& out_span,
                                ValueKind& out_kind) noexcept {
  const char c = s[i];
  const std::string_view word = keyword_starting_with(c);
  if (s.substr(i, word.size()) != word) {
    return false;
  }
  out_span = s.substr(i, word.size());
  out_kind = (c == 'n') ? ValueKind::null : ValueKind::boolean;
  i += word.size();
  return true;
}

constexpr std::size_t kMaxCheckedJsonDepth = 16;

[[nodiscard]] bool is_hex_digit(char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// Unlike skip_string(), this validates escapes and control characters. JSON
// unicode escapes are structurally valid in values, but the protocol's typed
// string reader can still reject them when a command consumes that value.
// Member names use the protocol's literal ASCII names, so unicode escapes are
// not accepted there.
[[nodiscard]] bool skip_checked_string(std::string_view s, std::size_t& i,
                                       std::string_view& out_raw,
                                       bool allow_unicode_escape) noexcept {
  if (i >= s.size() || s[i] != '"') {
    return false;
  }
  const std::size_t start = ++i;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x20) {
      return false;
    }
    if (c == '"') {
      out_raw = s.substr(start, i - start);
      ++i;
      return true;
    }
    if (c != '\\') {
      ++i;
      continue;
    }
    if (i + 1 >= s.size()) {
      return false;
    }
    const char escape = s[i + 1];
    if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' ||
        escape == 'f' || escape == 'n' || escape == 'r' || escape == 't') {
      i += 2;
      continue;
    }
    if (escape != 'u' || !allow_unicode_escape || i + 5 >= s.size()) {
      return false;
    }
    for (std::size_t digit = i + 2; digit <= i + 5; ++digit) {
      if (!is_hex_digit(s[digit])) {
        return false;
      }
    }
    i += 6;
  }
  return false;
}

// The branching here is JSON's number grammar transcribed: an optional sign, an
// integer part that may not have a leading zero, an optional fraction and an
// optional exponent, each with its own "at least one digit" rule. Splitting it
// into a helper per clause would lower the measured complexity without lowering
// the real complexity, and would move the rules away from each other -- which
// is what makes a grammar hard to check against a specification.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] bool skip_checked_number(std::string_view s, std::size_t& i,
                                       std::string_view& out_span) noexcept {
  const std::size_t start = i;
  if (i < s.size() && s[i] == '-') {
    ++i;
  }
  if (i >= s.size()) {
    return false;
  }
  if (s[i] == '0') {
    ++i;
    if (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      return false;
    }
  } else {
    if (s[i] < '1' || s[i] > '9') {
      return false;
    }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      ++i;
    }
  }
  if (i < s.size() && s[i] == '.') {
    ++i;
    if (i >= s.size() || s[i] < '0' || s[i] > '9') {
      return false;
    }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      ++i;
    }
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
      ++i;
    }
    if (i >= s.size() || s[i] < '0' || s[i] > '9') {
      return false;
    }
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      ++i;
    }
  }
  out_span = s.substr(start, i - start);
  return true;
}

// The four functions below form one recursive-descent group, because JSON's
// grammar is recursive: a value may be an object, whose members are values.
//
// misc-no-recursion is suppressed at each of them rather than switched off for
// the project, because recursion on a device with a small stack is worth being
// told about everywhere else. What makes it safe *here* is the bound: every
// descent goes through skip_checked_value, which refuses at
// kMaxCheckedJsonDepth, so the stack cost has a ceiling that does not depend on
// the input. test_nesting_is_bounded_and_the_boundary_is_where_it_should_be, in
// test_native_json_scan, holds that ceiling from both sides, so the claim in
// this comment fails a test if it stops being true.
[[nodiscard]] bool skip_checked_value(std::string_view s, std::size_t& i,
                                      std::string_view& out_span,
                                      ValueKind& out_kind,
                                      std::size_t depth,
                                      bool check_duplicates) noexcept;

[[nodiscard]] bool checked_key_appeared_before(
    std::string_view object, std::size_t object_start, std::size_t before,
    std::string_view key, std::size_t depth) noexcept;

// NOLINTNEXTLINE(misc-no-recursion) -- bounded; see the note above
[[nodiscard]] bool skip_checked_object(std::string_view s, std::size_t& i,
                                       std::string_view& out_span,
                                       std::size_t depth,
                                       bool check_duplicates) noexcept {
  const std::size_t start = i++;
  eat_ws(s, i);
  if (i < s.size() && s[i] == '}') {
    ++i;
    out_span = s.substr(start, i - start);
    return true;
  }
  while (i < s.size()) {
    const std::size_t member_offset = i;
    std::string_view member_key;
    if (!skip_checked_string(s, i, member_key, false)) {
      return false;
    }
    if (check_duplicates &&
        checked_key_appeared_before(s, start, member_offset, member_key,
                                    depth)) {
      return false;
    }
    eat_ws(s, i);
    if (i >= s.size() || s[i] != ':') {
      return false;
    }
    ++i;
    eat_ws(s, i);

    std::string_view member_span;
    ValueKind member_kind = ValueKind::null;
    if (!skip_checked_value(s, i, member_span, member_kind, depth,
                            check_duplicates)) {
      return false;
    }
    eat_ws(s, i);
    if (i >= s.size()) {
      return false;
    }
    if (s[i] == '}') {
      ++i;
      out_span = s.substr(start, i - start);
      return true;
    }
    if (s[i] != ',') {
      return false;
    }
    ++i;
    eat_ws(s, i);
    if (i >= s.size() || s[i] == '}') {
      return false;
    }
  }
  return false;
}

// NOLINTNEXTLINE(misc-no-recursion) -- bounded; see the note above
[[nodiscard]] bool skip_checked_array(std::string_view s, std::size_t& i,
                                      std::string_view& out_span,
                                      std::size_t depth,
                                      bool check_duplicates) noexcept {
  const std::size_t start = i++;
  eat_ws(s, i);
  if (i < s.size() && s[i] == ']') {
    ++i;
    out_span = s.substr(start, i - start);
    return true;
  }
  while (i < s.size()) {
    std::string_view element_span;
    ValueKind element_kind = ValueKind::null;
    if (!skip_checked_value(s, i, element_span, element_kind, depth,
                            check_duplicates)) {
      return false;
    }
    eat_ws(s, i);
    if (i >= s.size()) {
      return false;
    }
    if (s[i] == ']') {
      ++i;
      out_span = s.substr(start, i - start);
      return true;
    }
    if (s[i] != ',') {
      return false;
    }
    ++i;
    eat_ws(s, i);
    if (i >= s.size() || s[i] == ']') {
      return false;
    }
  }
  return false;
}

// NOLINTNEXTLINE(misc-no-recursion) -- bounded; see the note above
[[nodiscard]] bool skip_checked_value(std::string_view s, std::size_t& i,
                                      std::string_view& out_span,
                                      ValueKind& out_kind,
                                      std::size_t depth,
                                      bool check_duplicates) noexcept {
  if (i >= s.size()) {
    return false;
  }
  const char c = s[i];
  if (c == '"') {
    out_kind = ValueKind::string;
    return skip_checked_string(s, i, out_span, true);
  }
  if (c == '{' || c == '[') {
    if (depth >= kMaxCheckedJsonDepth) {
      return false;
    }
    out_kind = c == '{' ? ValueKind::object : ValueKind::array;
    return c == '{'
               ? skip_checked_object(s, i, out_span, depth + 1,
                                     check_duplicates)
               : skip_checked_array(s, i, out_span, depth + 1,
                                    check_duplicates);
  }
  if (c == 't' || c == 'f' || c == 'n') {
    return skip_keyword(s, i, out_span, out_kind);
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    out_kind = ValueKind::number;
    return skip_checked_number(s, i, out_span);
  }
  return false;
}

// Re-walk the already validated prefix instead of imposing an arbitrary
// member-count limit or allocating storage. Request bodies are bounded, so
// the quadratic worst case remains small and duplicate names fail closed.
// NOLINTNEXTLINE(misc-no-recursion) -- bounded; see the note above
[[nodiscard]] bool checked_key_appeared_before(std::string_view object,
                                               std::size_t object_start,
                                               std::size_t before,
                                               std::string_view key,
                                               std::size_t depth) noexcept {
  std::size_t i = object_start;
  if (i >= object.size() || object[i] != '{') {
    return true;
  }
  ++i;
  eat_ws(object, i);

  while (i < before) {
    std::string_view earlier_key;
    if (!skip_checked_string(object, i, earlier_key, false)) {
      return true;
    }
    if (earlier_key == key) {
      return true;
    }
    eat_ws(object, i);
    if (i >= before || object[i] != ':') {
      return true;
    }
    ++i;
    eat_ws(object, i);

    std::string_view ignored_span;
    ValueKind ignored_kind = ValueKind::null;
    // The ordinary walk already validated these earlier subtrees. Rechecking
    // duplicates while re-walking a prefix would recursively repeat the same
    // prefixes and make nested inputs unnecessarily expensive.
    if (!skip_checked_value(object, i, ignored_span, ignored_kind, depth,
                            false)) {
      return true;
    }
    eat_ws(object, i);
    if (i >= before) {
      return i != before;
    }
    if (object[i] != ',') {
      return true;
    }
    ++i;
    eat_ws(object, i);
  }
  return i != before;
}

// Skip the value at i. On success i lands just past it, and its extent and
// kind are returned.
[[nodiscard]] bool skip_value(std::string_view s, std::size_t& i,
                              std::string_view& out_span, ValueKind& out_kind) noexcept {
  if (i >= s.size()) {
    return false;
  }
  const char c = s[i];
  if (c == '"') {
    out_kind = ValueKind::string;
    return skip_string(s, i, out_span);  // for a string, the span is its contents
  }
  if (c == '{' || c == '[') {
    out_kind = (c == '{') ? ValueKind::object : ValueKind::array;
    return skip_container(s, i, out_span);
  }
  if (c == 't' || c == 'f' || c == 'n') {
    return skip_keyword(s, i, out_span, out_kind);
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    out_kind = ValueKind::number;
    return skip_number(s, i, out_span);
  }
  return false;
}

// Find key at the top level of object, returning the extent and kind of its
// value.
[[nodiscard]] bool find_member(std::string_view object, std::string_view key,
                               std::string_view& out_span,
                               ValueKind& out_kind) noexcept {
  std::size_t i = 0;
  eat_ws(object, i);
  if (i >= object.size() || object[i] != '{') {
    return false;
  }
  ++i;

  while (i < object.size()) {
    while (i < object.size() && (is_ws(object[i]) || object[i] == ',')) {
      ++i;
    }
    if (i < object.size() && object[i] == '}') {
      return false;  // not found by the end of the object
    }
    if (i >= object.size() || object[i] != '"') {
      return false;  // a key is always a string
    }

    std::string_view member_key;
    if (!skip_string(object, i, member_key)) {
      return false;
    }
    eat_ws(object, i);
    if (i >= object.size() || object[i] != ':') {
      return false;
    }
    ++i;
    eat_ws(object, i);

    std::string_view span;
    ValueKind kind = ValueKind::null;
    if (!skip_value(object, i, span, kind)) {
      return false;
    }
    // Keys are compared raw: the contract's keys are ASCII and unescaped.
    if (member_key == key) {
      out_span = span;
      out_kind = kind;
      return true;
    }
  }
  return false;
}

}  // namespace

bool json_find_raw_string(std::string_view object, std::string_view key,
                          std::string_view& out_raw) noexcept {
  std::string_view span;
  ValueKind kind = ValueKind::null;
  if (!find_member(object, key, span, kind) || kind != ValueKind::string) {
    return false;
  }
  out_raw = span;
  return true;
}

bool json_find_object(std::string_view object, std::string_view key,
                      std::string_view& out_object) noexcept {
  std::string_view span;
  ValueKind kind = ValueKind::null;
  if (!find_member(object, key, span, kind) || kind != ValueKind::object) {
    return false;
  }
  out_object = span;
  return true;
}

// Same reasoning as skip_checked_number: this walks an object's members and has
// to distinguish "absent", "present but not an object" and "malformed" at every
// step, because a caller that cannot tell those apart cannot fail closed. The
// branches are the specification, and test_native_json_scan covers them.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
JsonMemberResult json_find_object_checked(std::string_view object,
                                          std::string_view key,
                                          std::string_view& out_object) noexcept {
  std::size_t i = 0;
  eat_ws(object, i);
  if (i >= object.size() || object[i] != '{') {
    return JsonMemberResult::invalid;
  }
  const std::size_t object_start = i;
  ++i;
  eat_ws(object, i);

  bool found = false;
  std::string_view candidate;

  if (i < object.size() && object[i] == '}') {
    ++i;
    eat_ws(object, i);
    return i == object.size() ? JsonMemberResult::missing
                              : JsonMemberResult::invalid;
  }

  while (i < object.size()) {
    if (object[i] != '"') {
      return JsonMemberResult::invalid;
    }

    const std::size_t member_offset = i;
    std::string_view member_key;
    if (!skip_checked_string(object, i, member_key, false)) {
      return JsonMemberResult::invalid;
    }
    if (checked_key_appeared_before(object, object_start, member_offset,
                                    member_key, 0)) {
      return JsonMemberResult::invalid;
    }
    eat_ws(object, i);
    if (i >= object.size() || object[i] != ':') {
      return JsonMemberResult::invalid;
    }
    ++i;
    eat_ws(object, i);

    std::string_view span;
    ValueKind kind = ValueKind::null;
    if (!skip_checked_value(object, i, span, kind, 0, true)) {
      return JsonMemberResult::invalid;
    }

    if (member_key == key) {
      if (found || kind != ValueKind::object) {
        return JsonMemberResult::invalid;
      }
      found = true;
      candidate = span;
    }

    eat_ws(object, i);
    if (i >= object.size()) {
      return JsonMemberResult::invalid;
    }
    if (object[i] == '}') {
      ++i;
      eat_ws(object, i);
      if (i != object.size()) {
        return JsonMemberResult::invalid;
      }
      if (found) {
        out_object = candidate;
        return JsonMemberResult::found;
      }
      return JsonMemberResult::missing;
    }
    if (object[i] != ',') {
      return JsonMemberResult::invalid;
    }
    ++i;
    eat_ws(object, i);
    if (i >= object.size() || object[i] == '}') {
      return JsonMemberResult::invalid;
    }
  }
  return JsonMemberResult::invalid;
}

bool json_find_integer(std::string_view object, std::string_view key,
                       std::int64_t& out) noexcept {
  std::string_view span;
  ValueKind kind = ValueKind::null;
  if (!find_member(object, key, span, kind) || kind != ValueKind::number) {
    return false;
  }
  // Integers only. The envelope never carries fractions or exponents, so
  // anything else is a failure rather than something to round.
  bool negative = false;
  std::size_t i = 0;
  if (i < span.size() && span[i] == '-') {
    negative = true;
    ++i;
  }
  if (i >= span.size()) {
    return false;
  }
  std::int64_t value = 0;
  for (; i < span.size(); ++i) {
    const char c = span[i];
    if (c < '0' || c > '9') {
      return false;
    }
    value = (value * 10) + (c - '0');
  }
  out = negative ? -value : value;
  return true;
}

bool json_find_bool(std::string_view object, std::string_view key, bool& out) noexcept {
  std::string_view span;
  ValueKind kind = ValueKind::null;
  if (!find_member(object, key, span, kind) || kind != ValueKind::boolean) {
    return false;
  }
  out = (span == "true");
  return true;
}

std::size_t json_unescape(std::string_view raw, char* out, std::size_t capacity) noexcept {
  if (out == nullptr) {
    return SIZE_MAX;
  }
  std::size_t written = 0;
  std::size_t i = 0;
  while (i < raw.size()) {
    char c = raw[i];
    if (c == '\\') {
      if (i + 1 >= raw.size()) {
        return SIZE_MAX;  // the escape is cut off
      }
      const char e = raw[i + 1];
      switch (e) {
        case '"':
          c = '"';
          break;
        case '\\':
          c = '\\';
          break;
        case '/':
          c = '/';
          break;
        case 'b':
          c = '\b';
          break;
        case 'f':
          c = '\f';
          break;
        case 'n':
          c = '\n';
          break;
        case 'r':
          c = '\r';
          break;
        case 't':
          c = '\t';
          break;
        default:
          // An escape the contract does not permit, such as \uXXXX.
          // Better counted as a failure than returned as mangled text.
          return SIZE_MAX;
      }
      i += 2;
    } else {
      ++i;
    }
    if (written >= capacity) {
      return SIZE_MAX;
    }
    out[written++] = c;
  }
  return written;
}

}  // namespace stackchan::domain
