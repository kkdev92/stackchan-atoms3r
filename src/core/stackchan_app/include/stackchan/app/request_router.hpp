#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "stackchan/app/command_registry.hpp"
#include "stackchan/domain/access_token.hpp"
#include "stackchan/domain/protocol.hpp"

// The entry point for API requests: authorise, parse the envelope,
// dispatch, and build the response.
//
// Why it is all in one place
// --------------------------
// Each of these steps can fail, and each failure has to produce a specific
// envelope: a rejected token, an oversized body, a missing command name, a
// payload that did not fit. Spread across an HTTP handler, none of that can
// be checked without a running device and a real POST. Gathered here, every
// one of those paths is a host test.
//
// What stays outside is transport only: pull the body and the header out of
// the HTTP request, send back what route() returned. No decisions.
//
// How a response is built
// -----------------------
// The payload is written first, and only then is the envelope wrapped
// around it, because whether the response is a success depends on how
// writing the payload went. Doing it the other way round means discovering
// the failure after "ok":true has already been emitted.
//
// Authorisation says what is missing but never hints at the correct value.
//
// Every return value is a complete JSON envelope, including the failures,
// so the caller never has to branch.
//
// Two deliberate strictnesses
// ---------------------------
// An id longer than the limit, or one containing an escape the contract
// does not allow, is treated as absent rather than truncated. A truncated
// id used for matching would eventually collide with a different request.
//
// Lifetime and concurrency
// ------------------------
// The returned view points into an internal buffer and is valid **until the
// next call to route()**. Handlers run one at a time on a single task,
// which is what makes that safe. Do not call this from two tasks at once.

namespace stackchan::app {

class RequestRouter {
 public:
  // Largest body accepted. Every command's arguments are small.
  static constexpr std::size_t kMaxBodyBytes = 2048;
  // Room for a payload. describe is the largest, and it grows as commands
  // are added.
  static constexpr std::size_t kPayloadBytes = 4096;
  static constexpr std::size_t kEnvelopeBytes = kPayloadBytes + 256;
  // Longest id, excluding the terminator. Envelope ids are short values
  // used for matching a response to its request.
  static constexpr std::size_t kMaxIdBytes = 63;

  // The registry and the token are held by reference, so the caller must
  // keep them alive.
  RequestRouter(const CommandRegistry& registry,
                const domain::AccessToken& token) noexcept
      : registry_(registry), token_(token) {}

  RequestRouter(const RequestRouter&) = delete;
  RequestRouter& operator=(const RequestRouter&) = delete;
  RequestRouter(RequestRouter&&) = delete;
  RequestRouter& operator=(RequestRouter&&) = delete;
  ~RequestRouter() = default;

  // Handle one request.
  //
  //   presented_token  the value of the authorisation header, or empty
  //   body             the HTTP body, unmodified
  //
  // Always returns a complete envelope.
  [[nodiscard]] std::string_view route(std::string_view presented_token,
                                       std::string_view body) noexcept;

 private:
  [[nodiscard]] std::string_view error(std::string_view id, domain::ErrorCode code,
                                       std::string_view message = {}) noexcept;

  const CommandRegistry& registry_;
  const domain::AccessToken& token_;

  std::array<char, kMaxIdBytes + 1> id_text_{};
  std::array<char, 64> name_text_{};
  std::array<char, kPayloadBytes> payload_buffer_{};
  std::array<char, kEnvelopeBytes> envelope_buffer_{};
};

}  // namespace stackchan::app
