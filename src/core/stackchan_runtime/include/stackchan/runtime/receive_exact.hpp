#pragma once

#include <cstddef>
#include <cstdint>

namespace stackchan::runtime {

enum class ReceiveExactResult : std::uint8_t { complete, incomplete, error };

// Fill a bounded destination even when the transport returns the data in
// several chunks. A zero result means the input ended early. A negative or
// over-reported result is a transport error. Callers ignore partial data in
// either case and preserve any transport-specific cleanup requirements.
// `receive` is a forwarding reference so that a caller can pass a temporary
// lambda, but it is deliberately never forwarded: the loop below calls it once
// per chunk, and std::forward would permit the first call to move from it and
// leave the rest reading a moved-from callable.
template <typename Receive>
[[nodiscard]] ReceiveExactResult receive_exact(
    char* destination, std::size_t expected,
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward) -- see above
    Receive&& receive) {
  if (destination == nullptr && expected != 0) {
    return ReceiveExactResult::error;
  }

  std::size_t received = 0;
  while (received < expected) {
    const int chunk = receive(destination + received, expected - received);
    if (chunk < 0) {
      return ReceiveExactResult::error;
    }
    if (chunk == 0) {
      return ReceiveExactResult::incomplete;
    }

    const auto chunk_size = static_cast<std::size_t>(chunk);
    if (chunk_size > expected - received) {
      return ReceiveExactResult::error;
    }
    received += chunk_size;
  }
  return ReceiveExactResult::complete;
}

}  // namespace stackchan::runtime
