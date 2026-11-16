#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <instantee/diagnostics/error.hpp>
#include <memory>
#include <optional>
#include <span>

namespace instantee::telemetry_apple {

// Allowlisted process counters only. No process/device identity is collected.
// Memory is bytes; user/system CPU are process-all-threads nanoseconds.
struct ResourceSample {
  std::uint64_t observed_ns = 0;
  std::optional<std::uint64_t> user_ns, system_ns;
  std::optional<std::uint64_t> rss_current_bytes, rss_peak_bytes;
  std::optional<std::uint64_t> physical_footprint_bytes;
};
[[nodiscard]] ResourceSample sample_process(std::uint64_t observed_ns) noexcept;

// One joining sampler thread and a preallocated bounded sample array. Stop
// wakes its timed wait. Access samples only after finish(); destruction also
// stops and joins. Capacity overflow or missed periods count as lost samples.
class ProcessSampler {
public:
  [[nodiscard]] static std::expected<std::unique_ptr<ProcessSampler>,
                                     spike::Error>
  start(std::chrono::steady_clock::time_point epoch, std::size_t capacity,
        std::chrono::milliseconds period = std::chrono::milliseconds{100});
  ~ProcessSampler();
  ProcessSampler(const ProcessSampler &) = delete;
  ProcessSampler &operator=(const ProcessSampler &) = delete;
  void finish() noexcept;
  [[nodiscard]] std::span<const ResourceSample> samples() const noexcept;
  [[nodiscard]] std::uint64_t lost_samples() const noexcept;

private:
  struct Impl;
  explicit ProcessSampler(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace instantee::telemetry_apple
