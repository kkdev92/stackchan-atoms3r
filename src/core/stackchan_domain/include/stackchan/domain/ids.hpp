#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Identifiers, for jobs, blobs, and a boot itself.
//
// What they have to survive
// -------------------------
// An identifier based on time since power-on repeats after a restart, so
// two different recordings can end up sharing one. Whoever asked for the
// first then receives the second, and nothing about that looks like an
// error.
//
// So an identifier here is a random per-boot value plus a counter that only
// ever increases within that boot:
//
//   boot_id  = identifies one power-on. Random, so a restart always changes
//              it
//   sequence = increases within that boot. Never goes backwards
//
// Deliberately no wall-clock time. This device has no clock of its own and
// does not know the date until Wi-Fi is up, but identifiers are needed
// before that. Formats such as ULID embed a timestamp, which here would
// mean inventing one. "Which boot, and how far into it" is both honest and
// enough.

namespace stackchan::domain {

// Identifies one power-on.
//
// 128 random bits in Crockford's base32: 26 characters, upper case, with I,
// L, O and U left out so that 1 and I, or 0 and O, cannot be confused. That
// matters because these end up in logs that people read aloud and retype.
class BootId {
 public:
  static constexpr std::size_t kTextLength = 26;

  // Built from randomness the caller supplies, so that no hardware
  // dependency reaches core.
  [[nodiscard]] static BootId from_entropy(std::uint64_t high,
                                           std::uint64_t low) noexcept;

  // All zeroes, meaning "not yet determined".
  [[nodiscard]] static BootId unset() noexcept { return BootId{}; }

  [[nodiscard]] bool is_set() const noexcept { return high_ != 0 || low_ != 0; }

  // 26 characters, not counting a terminator.
  [[nodiscard]] std::string_view text() const noexcept {
    return std::string_view{text_.data(), kTextLength};
  }

  [[nodiscard]] bool operator==(const BootId& other) const noexcept {
    return high_ == other.high_ && low_ == other.low_;
  }
  [[nodiscard]] bool operator!=(const BootId& other) const noexcept {
    return !(*this == other);
  }

 private:
  BootId() noexcept;

  std::uint64_t high_ = 0;
  std::uint64_t low_ = 0;
  std::array<char, kTextLength> text_{};
};

  // Unique within one boot.
  //
  // Formatted "<boot_id>-<sequence>", so which boot it belongs to is
  // visible at a glance. The sequence is not zero-padded, because padding
  // would impose a maximum.
class Id {
 public:
  // 26 characters of boot_id, a '-', and up to 20 digits of sequence.
  static constexpr std::size_t kMaxTextLength = BootId::kTextLength + 1 + 20;

  [[nodiscard]] std::string_view text() const noexcept {
    return std::string_view{text_.data(), length_};
  }

  [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }

  [[nodiscard]] bool operator==(const Id& other) const noexcept {
    return text() == other.text();
  }

 private:
  friend class IdGenerator;
  Id(const BootId& boot, std::uint64_t sequence) noexcept;

  std::array<char, kMaxTextLength> text_{};
  std::size_t length_ = 0;
  std::uint64_t sequence_ = 0;
};

// Hands out identifiers. One per boot.
class IdGenerator {
 public:
  explicit IdGenerator(const BootId& boot) noexcept : boot_(boot) {}

  // The next identifier. Increases on every call.
  [[nodiscard]] Id next() noexcept { return Id{boot_, ++sequence_}; }

  [[nodiscard]] const BootId& boot() const noexcept { return boot_; }

  // How many have been handed out so far.
  [[nodiscard]] std::uint64_t issued() const noexcept { return sequence_; }

 private:
  BootId boot_;
  std::uint64_t sequence_ = 0;
};

}  // namespace stackchan::domain
