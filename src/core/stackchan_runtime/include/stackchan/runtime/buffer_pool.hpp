#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// A pool of fixed-size blocks, lent out one at a time. Used to carry audio
// PCM and HTTP bodies.
//
// Why it exists
// -------------
// To keep the heap from fragmenting. Audio allocates and frees a few
// kilobytes every few tens of milliseconds. Repeated malloc and free leaves
// the total free space adequate while no large contiguous block remains.
// There are only 320 KB of internal RAM here, and once it is fragmented an
// allocation that Wi-Fi or TLS needs will fail and take the device down.
//
// Everything is allocated once at startup and only lent out afterwards. No
// malloc runs while the firmware is working, so nothing fragments.
//
// A block that is never returned is a block lost. in_use() and
// exhausted_count() exist so that a missing return can be spotted.
//
// Ownership
// ---------
// A Lease is the receipt, and returns its block when destroyed (RAII). It
// cannot be copied; a copy would return the same block twice.

namespace stackchan::runtime {

template <std::size_t BlockBytes, std::size_t BlockCount>
class BufferPool {
  static_assert(BlockBytes > 0, "a zero-length block is of no use");
  static_assert(BlockCount > 0, "a pool of zero blocks is of no use");

 public:
  class Lease;

  BufferPool() noexcept = default;
  ~BufferPool() = default;

  // The pool does not move: every Lease holds its address.
  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;
  BufferPool(BufferPool&&) = delete;
  BufferPool& operator=(BufferPool&&) = delete;

  [[nodiscard]] static constexpr std::size_t block_bytes() noexcept { return BlockBytes; }
  [[nodiscard]] static constexpr std::size_t block_count() noexcept { return BlockCount; }

  [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }
  [[nodiscard]] std::size_t available() const noexcept { return BlockCount - in_use_; }

  // The most blocks ever out at once. Kept so the pool size can be
  // reconsidered later against evidence.
  [[nodiscard]] std::size_t high_water_mark() const noexcept { return high_water_; }

  // How often the pool ran dry. Anything but zero means either too few
  // blocks or a leak.
  [[nodiscard]] std::size_t exhausted_count() const noexcept { return exhausted_; }

  // Take a block. Returns an invalid lease if none are free, so callers
  // must check valid(). Writing through an invalid lease does nothing.
  [[nodiscard]] Lease acquire() noexcept;

  // The receipt. Returns its block when destroyed.
  class Lease {
   public:
    Lease() noexcept = default;
    ~Lease() { release(); }

    // No copying: it would return the same block twice.
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    // Moving is allowed, so a lease can be handed between tasks.
    Lease(Lease&& other) noexcept
        : pool_(other.pool_), index_(other.index_), valid_(other.valid_) {
      other.valid_ = false;
    }

    Lease& operator=(Lease&& other) noexcept {
      if (this != &other) {
        release();
        pool_ = other.pool_;
        index_ = other.index_;
        valid_ = other.valid_;
        other.valid_ = false;
      }
      return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    explicit operator bool() const noexcept { return valid_; }

    [[nodiscard]] std::uint8_t* data() noexcept {
      return valid_ ? pool_->blocks_[index_].data() : nullptr;
    }

    [[nodiscard]] const std::uint8_t* data() const noexcept {
      return valid_ ? pool_->blocks_[index_].data() : nullptr;
    }

    [[nodiscard]] static constexpr std::size_t size() noexcept { return BlockBytes; }

    // Return the block early, before the lease goes out of scope.
    void release() noexcept {
      if (valid_) {
        pool_->give_back(index_);
        valid_ = false;
      }
    }

   private:
    friend class BufferPool;
    Lease(BufferPool* pool, std::size_t index) noexcept
        : pool_(pool), index_(index), valid_(true) {}

    BufferPool* pool_ = nullptr;
    std::size_t index_ = 0;
    bool valid_ = false;
  };

 private:
  void give_back(std::size_t index) noexcept {
    taken_[index] = false;
    --in_use_;
  }

  std::array<std::array<std::uint8_t, BlockBytes>, BlockCount> blocks_{};
  std::array<bool, BlockCount> taken_{};
  std::size_t in_use_ = 0;
  std::size_t high_water_ = 0;
  std::size_t exhausted_ = 0;
};

// -------------------------------------------------------- implementation

template <std::size_t BlockBytes, std::size_t BlockCount>
typename BufferPool<BlockBytes, BlockCount>::Lease
BufferPool<BlockBytes, BlockCount>::acquire() noexcept {
  for (std::size_t i = 0; i < BlockCount; ++i) {
    if (!taken_[i]) {
      taken_[i] = true;
      ++in_use_;
      high_water_ = std::max(in_use_, high_water_);
      return Lease{this, i};
    }
  }
  ++exhausted_;
  return Lease{};
}

}  // namespace stackchan::runtime
