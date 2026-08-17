#pragma once

#include <cstddef>
#include <cstdint>

namespace stackchan::domain {

enum class HealthLevel : std::uint8_t {
  healthy,
  degraded,
  critical,
};

struct MemorySnapshot {
  std::size_t internal_free;
  std::size_t largest_internal_block;
};

struct MemoryThresholds {
  std::size_t degraded_internal_free;
  std::size_t critical_internal_free;
  std::size_t critical_largest_block;
};

HealthLevel classify_memory(const MemorySnapshot& snapshot,
                            const MemoryThresholds& thresholds) noexcept;

}  // namespace stackchan::domain
