#pragma once

#include <cstdint>
#include <string_view>

// How a command reads its arguments.
//
// Why this is an abstraction
// --------------------------
// Parsing happens on the platform side. The body of a handler, though —
// the part that validates values and calls into a feature — is worth
// verifying on the host. So only the reading is abstracted, which lets the
// handlers live in core.
//
// Tests plug in a trivial implementation; the device plugs in the real
// parser. The handler code is identical either way.
//
// Why "was it found" is reported
// ------------------------------
// Substituting a default for a missing argument lets a malformed request
// pass silently, and the caller never learns that what they sent was not
// what arrived. Reporting absence separately from value is what makes a
// typo in a field name an error rather than a surprise.

namespace stackchan::ports {

class ParamReader {
 public:
  virtual ~ParamReader() = default;

  ParamReader(const ParamReader&) = delete;
  ParamReader& operator=(const ParamReader&) = delete;
  ParamReader(ParamReader&&) = delete;
  ParamReader& operator=(ParamReader&&) = delete;

  // True if the value was retrieved. A type mismatch also yields false.
  [[nodiscard]] virtual bool read_string(std::string_view key,
                                         std::string_view& out) const = 0;
  [[nodiscard]] virtual bool read_integer(std::string_view key,
                                          std::int64_t& out) const = 0;
  [[nodiscard]] virtual bool read_bool(std::string_view key, bool& out) const = 0;

  // Whether there are any arguments at all. Distinguishes an empty payload
  // from one that carries no values.
  [[nodiscard]] virtual bool empty() const = 0;

 protected:
  ParamReader() = default;
};

}  // namespace stackchan::ports
