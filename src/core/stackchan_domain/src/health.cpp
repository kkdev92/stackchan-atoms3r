#include "stackchan/domain/health.hpp"

namespace stackchan::domain {

HealthLevel classify_memory(const MemorySnapshot& snapshot,
                            const MemoryThresholds& thresholds) noexcept {
  if (snapshot.internal_free <= thresholds.critical_internal_free ||
      snapshot.largest_internal_block <= thresholds.critical_largest_block) {
    return HealthLevel::critical;
  }

  if (snapshot.internal_free <= thresholds.degraded_internal_free) {
    return HealthLevel::degraded;
  }

  return HealthLevel::healthy;
}

}  // namespace stackchan::domain
