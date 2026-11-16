#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace instantee::runtime {

// Monotonic host nanoseconds from one process-local epoch. GPU completion is
// observed on the host; sink acceptance does not imply encode completion.
struct FrameSample {
  std::uint64_t frame = 0;
  std::uint64_t request_ns = 0;
  std::uint64_t admitted_ns = 0;
  std::uint64_t eval_start_ns = 0;
  std::uint64_t eval_done_ns = 0;
  std::uint64_t gpu_submit_ns = 0;
  std::uint64_t gpu_completed_observed_ns = 0;
  std::uint64_t sink_attempt_ns = 0;
  std::uint64_t sink_accepted_ns = 0;
};

struct LatencySummary {
  std::uint64_t sample_count, min, p50, p95, p99, max;
};

// Serial sink-owner log, preallocated before admission. No allocations during
// record(). Rejects duplicate, missing, out-of-order or invalid samples and
// counts loss. Read only after the sink has stopped. Summary includes every
// accepted sample, with exact nearest-rank quantiles and no warm-up removal.
class FrameSamples {
public:
  [[nodiscard]] static std::expected<FrameSamples, spike::Error>
  create(std::size_t capacity);
  [[nodiscard]] bool record(FrameSample sample) noexcept;
  [[nodiscard]] std::span<const FrameSample> samples() const noexcept;
  [[nodiscard]] std::uint64_t lost_samples() const noexcept { return lost_; }
  // Null when empty or any sample was lost. Sorting allocates only here.
  [[nodiscard]] std::optional<LatencySummary> summary() const;

private:
  explicit FrameSamples(std::vector<FrameSample> storage) noexcept;
  std::vector<FrameSample> storage_;
  std::size_t count_ = 0;
  std::uint64_t lost_ = 0;
};
} // namespace instantee::runtime
