#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

// A bounded hand-off box, for passing commands and events between tasks.
//
// Why it exists
// -------------
// Several tasks run at once here: the main loop, the conversation, the HTTP
// server. They need to hand work to each other without sharing mutable
// variables, because shared variables are where the races and the
// hard-to-reproduce faults come from. Passing a value through a box instead
// is how invariant 6 — do not add global mutable state — is achieved in
// practice.
//
// Why the capacity is fixed
// -------------------------
// Not to prevent overflow, but to notice it. A queue that grows without
// bound hides congestion as latency and eventually eats the heap. With a
// limit and a count of what was discarded, congestion becomes something
// that can be observed.
//
// Why it never blocks
// -------------------
// No OS primitives here; they would put this out of reach of the host
// tests. Waiting is the platform's job. This box only answers "does it
// fit", which leaves the fragile part — what happens when it does not —
// verifiable in seconds.

namespace stackchan::runtime {

// What to do when it is full.
enum class OverflowPolicy : std::uint8_t {
  // Discard the incoming value. For commands, where order carries meaning
  // and a later instruction must not flush an earlier one.
  reject_newest,
  // Discard the oldest. For state notifications, where only the latest
  // matters — a stale sensor reading is worth less than a fresh one.
  drop_oldest,
};

template <typename T, std::size_t Capacity>
class Mailbox {
  static_assert(Capacity > 0, "a zero-capacity mailbox is of no use");

 public:
  explicit Mailbox(OverflowPolicy policy = OverflowPolicy::reject_newest) noexcept
      : policy_(policy) {}

  ~Mailbox() = default;

  // The box stays where it was placed. A copy would add a second
  // destination and make "which one was it posted to" ambiguous.
  Mailbox(const Mailbox&) = delete;
  Mailbox& operator=(const Mailbox&) = delete;
  Mailbox(Mailbox&&) = delete;
  Mailbox& operator=(Mailbox&&) = delete;

  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }

  // Post a value. False if it did not fit.
  //
  // Under drop_oldest it evicts the oldest entry and returns true. Either
  // way, discarding anything increments dropped().
  [[nodiscard]] bool push(T value) noexcept;

  // Take a value, or nullopt if empty.
  [[nodiscard]] std::optional<T> pop() noexcept;

  // How many values were discarded. Anything but zero means something is
  // not keeping up.
  [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }

  // The most entries ever held at once. Kept so the capacity can be
  // reconsidered later against evidence.
  [[nodiscard]] std::size_t high_water_mark() const noexcept { return high_water_; }

  void clear() noexcept;

 private:
  std::array<T, Capacity> slots_{};
  std::size_t head_ = 0;  // where the next pop() reads from
  std::size_t size_ = 0;
  std::size_t dropped_ = 0;
  std::size_t high_water_ = 0;
  OverflowPolicy policy_;
};

// -------------------------------------------------------- implementation

template <typename T, std::size_t Capacity>
bool Mailbox<T, Capacity>::push(T value) noexcept {
  if (size_ == Capacity) {
    if (policy_ == OverflowPolicy::reject_newest) {
      ++dropped_;
      return false;
    }
    // drop_oldest: advance the head to make room.
    head_ = (head_ + 1) % Capacity;
    --size_;
    ++dropped_;
  }

  const std::size_t tail = (head_ + size_) % Capacity;
  slots_[tail] = std::move(value);
  ++size_;
  high_water_ = std::max(size_, high_water_);
  return true;
}

template <typename T, std::size_t Capacity>
std::optional<T> Mailbox<T, Capacity>::pop() noexcept {
  if (size_ == 0) {
    return std::nullopt;
  }
  std::optional<T> value{std::move(slots_[head_])};
  slots_[head_] = T{};  // clear it, so no reference or resource is held
  head_ = (head_ + 1) % Capacity;
  --size_;
  return value;
}

template <typename T, std::size_t Capacity>
void Mailbox<T, Capacity>::clear() noexcept {
  while (pop().has_value()) {
  }
}

}  // namespace stackchan::runtime
