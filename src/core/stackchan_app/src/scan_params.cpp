#include "stackchan/app/scan_params.hpp"

#include "stackchan/domain/json_scan.hpp"

namespace stackchan::app {

bool ScanParams::read_string(std::string_view key, std::string_view& out) const {
  std::string_view raw;
  if (!domain::json_find_raw_string(payload_, key, raw)) {
    return false;
  }
  char* slot = arena_.data() + arena_used_;
  const std::size_t room = arena_.size() - arena_used_;
  const std::size_t written = domain::json_unescape(raw, slot, room);
  if (written == SIZE_MAX) {
    // Either the arena ran out or the string uses an escape the contract
    // forbids. Do not hand back a partially unescaped value.
    return false;
  }
  arena_used_ += written;
  out = std::string_view{slot, written};
  return true;
}

bool ScanParams::read_integer(std::string_view key, std::int64_t& out) const {
  return domain::json_find_integer(payload_, key, out);
}

bool ScanParams::read_bool(std::string_view key, bool& out) const {
  return domain::json_find_bool(payload_, key, out);
}

bool ScanParams::empty() const {
  // "No payload at all" and "an empty object" are both treated as empty.
  if (payload_.empty()) {
    return true;
  }
  // Is there a key — an opening quote — after the '{' and before the '}'?
  bool inside = false;
  for (const char c : payload_) {
    if (!inside) {
      if (c == '{') {
        inside = true;
      }
      continue;
    }
    if (c == '"') {
      return false;  // there is a key
    }
    if (c == '}') {
      return true;
    }
    // Anything but whitespace here means the shape is invalid. Report it as
    // non-empty so optional-argument handlers reject it instead of applying
    // their no-argument behavior.
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      return false;
    }
  }
  return true;
}

}  // namespace stackchan::app
