#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/frame_samples.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace instantee::runtime {

// How a GPU duration was obtained. Host completion callbacks are never device
// execution time (BENCHMARK_PROTOCOL / SAS §17).
enum class GpuTimingKind : std::uint8_t {
  unavailable = 0,
  host_completion_observed = 1,
  device_counter = 2,
};

inline constexpr std::string_view kCpuScopeRendererAllThreads =
    "renderer_process_all_threads";

struct FrameLatencyBreakdown {
  std::uint64_t frame_latency_ns = 0;     // sink_accepted - request
  std::uint64_t admission_wait_ns = 0;    // admitted - request
  std::uint64_t admitted_latency_ns = 0;  // sink_accepted - admitted
  std::uint64_t eval_ns = 0;              // eval_done - eval_start
  std::uint64_t gpu_host_observed_ns = 0; // completed_observed - submit
  GpuTimingKind gpu_host_timing_kind = GpuTimingKind::unavailable;
  // Only set from a real device timing API. Never derived from host notify.
  std::optional<std::uint64_t> gpu_device_time_ns{};
};

struct CpuTotals {
  std::uint64_t user_ns = 0;
  std::uint64_t system_ns = 0;
  std::uint64_t cpu_ns = 0;
  std::uint64_t wall_ns = 0;
  std::uint32_t logical_cpu_count = 0;
  std::uint64_t verified_output_frames = 0;
  double cpu_seconds = 0.0;
  double cpu_core_equivalents = 0.0;
  double cpu_percent_one_core = 0.0;
  double cpu_percent_machine = 0.0;
  // Null when verified_output_frames == 0.
  std::optional<double> cpu_seconds_per_verified_frame{};
  std::string_view cpu_scope = kCpuScopeRendererAllThreads;
};

// Platform-neutral memory observation for formula tests. Apple adapters map
// OS samples into these fields; units are always bytes.
struct MemoryObservation {
  std::optional<std::uint64_t> rss_current_bytes{};
  std::optional<std::uint64_t> rss_peak_bytes{};
  std::optional<std::uint64_t> physical_footprint_bytes{};
};

struct MemoryPeaks {
  std::optional<std::uint64_t> rss_peak_bytes{};
  std::optional<std::uint64_t> physical_footprint_peak_bytes{};
  std::string_view rss_unit = "bytes";
  std::string_view footprint_unit = "bytes";
};

// Exact nearest-rank over a value series. Null when empty or lost_samples > 0.
[[nodiscard]] std::optional<LatencySummary>
exact_nearest_rank(std::span<const std::uint64_t> values,
                   std::uint64_t lost_samples) noexcept;

[[nodiscard]] std::expected<FrameLatencyBreakdown, diagnostics::Error>
breakdown_frame_sample(const FrameSample &sample) noexcept;

// CPU formulas from BENCHMARK_PROTOCOL §5. wall_ns and logical_cpu_count must
// be > 0; zero/failed sources are rejected (not reported as measured zero).
[[nodiscard]] std::expected<CpuTotals, diagnostics::Error>
compute_cpu_totals(std::uint64_t user_ns, std::uint64_t system_ns,
                   std::uint64_t wall_ns, std::uint32_t logical_cpu_count,
                   std::uint64_t verified_output_frames) noexcept;

// Peak RSS/footprint across observations. Missing fields stay null; a null
// metric is never coerced to zero.
[[nodiscard]] MemoryPeaks
compute_memory_peaks(std::span<const MemoryObservation> samples) noexcept;

// Hot-path recorder: serial FrameSamples plus derived latency/CPU/memory
// summaries. Raw samples are retained without warm-up removal; any lost sample
// invalidates quantile summaries (certification contract).
class HotPathInstrument {
public:
  [[nodiscard]] static std::expected<HotPathInstrument, diagnostics::Error>
  create(std::size_t frame_capacity);

  [[nodiscard]] bool record(FrameSample sample) noexcept;

  [[nodiscard]] std::span<const FrameSample> samples() const noexcept;
  [[nodiscard]] std::uint64_t lost_samples() const noexcept;

  // Primary p99 interval: request → sink_accepted.
  [[nodiscard]] std::optional<LatencySummary> frame_latency_summary() const;
  [[nodiscard]] std::optional<LatencySummary> admission_wait_summary() const;
  [[nodiscard]] std::optional<LatencySummary> gpu_host_observed_summary() const;

  // Per-frame breakdown; device time stays null unless explicitly supplied.
  [[nodiscard]] std::expected<FrameLatencyBreakdown, diagnostics::Error>
  breakdown_at(std::size_t index) const noexcept;

  // Attach a device counter for one frame. Rejects if kind is not
  // device_counter or if host-observed kind is requested here.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  set_gpu_device_time_ns(std::size_t index, std::uint64_t device_time_ns);

private:
  explicit HotPathInstrument(FrameSamples frames) noexcept;

  FrameSamples frames_;
  std::vector<std::optional<std::uint64_t>> gpu_device_time_ns_;
};

} // namespace instantee::runtime
