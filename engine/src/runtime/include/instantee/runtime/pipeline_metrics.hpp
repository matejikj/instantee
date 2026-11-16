#pragma once

#include <cstdint>

namespace instantee::runtime {

// Wait/admission counters for bounded pipeline stages. Values are cumulative
// for the owning object lifetime unless explicitly reset by the owner.
struct PipelineMetrics {
  std::uint64_t high_water = 0;
  std::uint64_t blocked_push_count = 0;
  std::uint64_t blocked_pop_count = 0;
  std::uint64_t blocked_acquire_count = 0;
  std::uint64_t blocked_wait_ns = 0;
};

} // namespace instantee::runtime
