#include "stackchan/domain/json_writer.hpp"

#include <array>

namespace stackchan::domain {
namespace {

constexpr std::array<char, 16> kHex = {'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

}  // namespace

void JsonWriter::put(char c) noexcept {
  ++required_;
  if (overflowed_ || length_ + 1 > capacity_) {
    overflowed_ = true;
    return;
  }
  buffer_[length_++] = c;
}

void JsonWriter::put(std::string_view text) noexcept {
  for (const char c : text) {
    put(c);
  }
}

void JsonWriter::put_escaped(std::string_view text) noexcept {
  put('"');
  for (const char c : text) {
    switch (c) {
      case '"':
        put("\\\"");
        break;
      case '\\':
        put("\\\\");
        break;
      case '\n':
        put("\\n");
        break;
      case '\r':
        put("\\r");
        break;
      case '\t':
        put("\\t");
        break;
      case '\b':
        put("\\b");
        break;
      case '\f':
        put("\\f");
        break;
      default: {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20) {
          // A control character, which would be invalid JSON if emitted raw.
          put("\\u00");
          put(kHex[(byte >> 4) & 0x0F]);
          put(kHex[byte & 0x0F]);
        } else {
          // Anything from 0x80 up passes through unchanged. UTF-8 travels
          // fine as it is, and leaving it alone keeps the text readable.
          put(c);
        }
        break;
      }
    }
  }
  put('"');
}

void JsonWriter::separate() noexcept {
  if (needs_comma_) {
    put(',');
  }
}

void JsonWriter::open(char bracket, bool is_object) noexcept {
  separate();
  if (depth_ >= kMaxDepth) {
    // Too deep to keep track of, so treat it as an overflow rather than
    // emit something whose brackets cannot be matched.
    overflowed_ = true;
    return;
  }
  is_object_[depth_] = is_object;
  ++depth_;
  put(bracket);
  needs_comma_ = false;
}

void JsonWriter::close(char bracket, bool is_object) noexcept {
  if (depth_ == 0 || is_object_[depth_ - 1] != is_object) {
    // Closing something that was never opened, or closing it with the wrong
    // bracket. A caller error, so overflow rather than emit invalid JSON.
    overflowed_ = true;
    return;
  }
  --depth_;
  put(bracket);
  needs_comma_ = true;
}

void JsonWriter::begin_object() noexcept { open('{', true); }
void JsonWriter::end_object() noexcept { close('}', true); }
void JsonWriter::begin_array() noexcept { open('[', false); }
void JsonWriter::end_array() noexcept { close(']', false); }

void JsonWriter::key(std::string_view name) noexcept {
  separate();
  put_escaped(name);
  put(':');
  // A value directly after a key takes no comma.
  needs_comma_ = false;
}

void JsonWriter::value(std::string_view text) noexcept {
  separate();
  put_escaped(text);
  needs_comma_ = true;
}

void JsonWriter::value(bool flag) noexcept {
  separate();
  put(flag ? std::string_view{"true"} : std::string_view{"false"});
  needs_comma_ = true;
}

void JsonWriter::value(std::uint64_t number) noexcept {
  separate();
  if (number == 0) {
    put('0');
  } else {
    std::array<char, 20> reversed{};
    std::size_t count = 0;
    while (number > 0) {
      reversed[count++] = static_cast<char>('0' + (number % 10));
      number /= 10;
    }
    while (count > 0) {
      put(reversed[--count]);
    }
  }
  needs_comma_ = true;
}

void JsonWriter::value(std::int64_t number) noexcept {
  if (number < 0) {
    separate();
    put('-');
    needs_comma_ = false;
    // Negating the most negative value overflows, so work in unsigned and
    // subtract.
    const auto magnitude = static_cast<std::uint64_t>(-(number + 1)) + 1;
    value(magnitude);
    return;
  }
  value(static_cast<std::uint64_t>(number));
}

void JsonWriter::null_value() noexcept {
  separate();
  put("null");
  needs_comma_ = true;
}

void JsonWriter::raw_json(std::string_view json) noexcept {
  separate();
  put(json);
  needs_comma_ = true;
}

void JsonWriter::member(std::string_view name, std::string_view text) noexcept {
  key(name);
  value(text);
}

void JsonWriter::member(std::string_view name, bool flag) noexcept {
  key(name);
  value(flag);
}

void JsonWriter::member(std::string_view name, std::uint64_t number) noexcept {
  key(name);
  value(number);
}

void JsonWriter::member(std::string_view name, std::int64_t number) noexcept {
  key(name);
  value(number);
}

void JsonWriter::reset() noexcept {
  length_ = 0;
  required_ = 0;
  depth_ = 0;
  overflowed_ = false;
  needs_comma_ = false;
}

}  // namespace stackchan::domain
