#include "stackchan/app/health_payload.hpp"

namespace stackchan::app {

std::string_view to_string(domain::HealthLevel level) noexcept {
  // Every value is listed rather than defaulted, so that adding one makes
  // -Wswitch point here.
  switch (level) {
    case domain::HealthLevel::healthy:
      return "healthy";
    case domain::HealthLevel::degraded:
      return "degraded";
    case domain::HealthLevel::critical:
      return "critical";
  }
  return "critical";  // when unsure, do not report the milder level
}

void write_health_payload(domain::JsonWriter& out, std::uint64_t uptime_ms,
                          const domain::MemorySnapshot& snapshot,
                          std::uint64_t psram_free,
                          domain::HealthLevel level) noexcept {
  out.begin_object();
  out.member("uptime_ms", uptime_ms);
  out.member("level", to_string(level));
  out.key("memory");
  out.begin_object();
  out.member("internal_free", static_cast<std::uint64_t>(snapshot.internal_free));
  out.member("largest_internal_block",
             static_cast<std::uint64_t>(snapshot.largest_internal_block));
  out.member("psram_free", psram_free);
  out.end_object();
  out.end_object();
}

}  // namespace stackchan::app
