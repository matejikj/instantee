#include <instantee/runtime/hot_path_telemetry.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace instantee::runtime {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "hot_path_telemetry", message};
}

[[nodiscard]] bool sample_times_sorted(const FrameSample &sample) noexcept {
  const std::uint64_t times[] = {
      sample.request_ns,      sample.admitted_ns,
      sample.eval_start_ns,   sample.eval_done_ns,
      sample.gpu_submit_ns,   sample.gpu_completed_observed_ns,
      sample.sink_attempt_ns, sample.sink_accepted_ns};
  return std::is_sorted(std::begin(times), std::end(times));
}

[[nodiscard]] LatencySummary
summarize_sorted(std::vector<std::uint64_t> &ordered) noexcept {
  const std::size_t count = ordered.size();
  const auto quantile = [&](std::size_t percent) {
    const auto rank =
        (count / 100U) * percent + ((count % 100U) * percent + 99U) / 100U;
    return ordered[rank - 1U];
  };
  return LatencySummary{count,        ordered.front(), quantile(50),
                        quantile(95), quantile(99),    ordered.back()};
}

} // namespace

std::optional<LatencySummary>
exact_nearest_rank(std::span<const std::uint64_t> values,
                   std::uint64_t lost_samples) noexcept {
  if (values.empty() || lost_samples != 0) {
    return std::nullopt;
  }
  std::vector<std::uint64_t> ordered(values.begin(), values.end());
  std::ranges::sort(ordered);
  return summarize_sorted(ordered);
}

std::expected<FrameLatencyBreakdown, diagnostics::Error>
breakdown_frame_sample(const FrameSample &sample) noexcept {
  if (!sample_times_sorted(sample)) {
    return std::unexpected(
        make_invalid("invalid_frame_sample",
                     "frame sample timestamps must be non-decreasing"));
  }
  FrameLatencyBreakdown out;
  out.frame_latency_ns = sample.sink_accepted_ns - sample.request_ns;
  out.admission_wait_ns = sample.admitted_ns - sample.request_ns;
  out.admitted_latency_ns = sample.sink_accepted_ns - sample.admitted_ns;
  out.eval_ns = sample.eval_done_ns - sample.eval_start_ns;
  out.gpu_host_observed_ns =
      sample.gpu_completed_observed_ns - sample.gpu_submit_ns;
  out.gpu_host_timing_kind = GpuTimingKind::host_completion_observed;
  out.gpu_device_time_ns = std::nullopt;
  return out;
}

std::expected<CpuTotals, diagnostics::Error>
compute_cpu_totals(std::uint64_t user_ns, std::uint64_t system_ns,
                   std::uint64_t wall_ns, std::uint32_t logical_cpu_count,
                   std::uint64_t verified_output_frames) noexcept {
  if (wall_ns == 0) {
    return std::unexpected(make_invalid(
        "invalid_wall_time", "CPU totals require a positive measured wall_ns"));
  }
  if (logical_cpu_count == 0) {
    return std::unexpected(
        make_invalid("invalid_logical_cpu_count",
                     "CPU totals require logical_cpu_count > 0"));
  }
  if (user_ns > std::numeric_limits<std::uint64_t>::max() - system_ns) {
    return std::unexpected(make_invalid(
        "cpu_counter_overflow", "user_ns + system_ns overflows uint64"));
  }

  CpuTotals out;
  out.user_ns = user_ns;
  out.system_ns = system_ns;
  out.cpu_ns = user_ns + system_ns;
  out.wall_ns = wall_ns;
  out.logical_cpu_count = logical_cpu_count;
  out.verified_output_frames = verified_output_frames;
  out.cpu_seconds = static_cast<double>(out.cpu_ns) / 1e9;
  out.cpu_core_equivalents =
      static_cast<double>(out.cpu_ns) / static_cast<double>(wall_ns);
  out.cpu_percent_one_core = 100.0 * out.cpu_core_equivalents;
  out.cpu_percent_machine =
      out.cpu_percent_one_core / static_cast<double>(logical_cpu_count);
  if (verified_output_frames > 0) {
    out.cpu_seconds_per_verified_frame =
        out.cpu_seconds / static_cast<double>(verified_output_frames);
  }
  out.cpu_scope = kCpuScopeRendererAllThreads;
  if (!std::isfinite(out.cpu_seconds) ||
      !std::isfinite(out.cpu_core_equivalents) ||
      !std::isfinite(out.cpu_percent_one_core) ||
      !std::isfinite(out.cpu_percent_machine)) {
    return std::unexpected(make_invalid(
        "non_finite_cpu_totals", "CPU formula produced a non-finite value"));
  }
  return out;
}

MemoryPeaks
compute_memory_peaks(std::span<const MemoryObservation> samples) noexcept {
  MemoryPeaks peaks;
  for (const MemoryObservation &sample : samples) {
    const auto consider = [](std::optional<std::uint64_t> &peak,
                             std::optional<std::uint64_t> value) {
      if (!value.has_value()) {
        return;
      }
      if (!peak.has_value() || *value > *peak) {
        peak = value;
      }
    };
    // Prefer explicit peak when present; otherwise track current as a lower
    // bound for the series peak.
    if (sample.rss_peak_bytes.has_value()) {
      consider(peaks.rss_peak_bytes, sample.rss_peak_bytes);
    } else {
      consider(peaks.rss_peak_bytes, sample.rss_current_bytes);
    }
    consider(peaks.physical_footprint_peak_bytes,
             sample.physical_footprint_bytes);
  }
  return peaks;
}

HotPathInstrument::HotPathInstrument(FrameSamples frames) noexcept
    : frames_(std::move(frames)) {}

std::expected<HotPathInstrument, diagnostics::Error>
HotPathInstrument::create(std::size_t frame_capacity) {
  auto frames = FrameSamples::create(frame_capacity);
  if (!frames) {
    return std::unexpected(frames.error());
  }
  HotPathInstrument instrument{std::move(*frames)};
  instrument.gpu_device_time_ns_.assign(frame_capacity, std::nullopt);
  return instrument;
}

bool HotPathInstrument::record(FrameSample sample) noexcept {
  return frames_.record(sample);
}

std::span<const FrameSample> HotPathInstrument::samples() const noexcept {
  return frames_.samples();
}

std::uint64_t HotPathInstrument::lost_samples() const noexcept {
  return frames_.lost_samples();
}

std::optional<LatencySummary> HotPathInstrument::frame_latency_summary() const {
  return frames_.summary();
}

std::optional<LatencySummary>
HotPathInstrument::admission_wait_summary() const {
  if (frames_.lost_samples() != 0) {
    return std::nullopt;
  }
  std::vector<std::uint64_t> values;
  values.reserve(frames_.samples().size());
  for (const FrameSample &sample : frames_.samples()) {
    values.push_back(sample.admitted_ns - sample.request_ns);
  }
  return exact_nearest_rank(values, 0);
}

std::optional<LatencySummary>
HotPathInstrument::gpu_host_observed_summary() const {
  if (frames_.lost_samples() != 0) {
    return std::nullopt;
  }
  std::vector<std::uint64_t> values;
  values.reserve(frames_.samples().size());
  for (const FrameSample &sample : frames_.samples()) {
    values.push_back(sample.gpu_completed_observed_ns - sample.gpu_submit_ns);
  }
  return exact_nearest_rank(values, 0);
}

std::expected<FrameLatencyBreakdown, diagnostics::Error>
HotPathInstrument::breakdown_at(std::size_t index) const noexcept {
  const auto recorded = frames_.samples();
  if (index >= recorded.size()) {
    return std::unexpected(
        make_invalid("sample_index_oob", "frame sample index out of range"));
  }
  auto breakdown = breakdown_frame_sample(recorded[index]);
  if (!breakdown) {
    return std::unexpected(breakdown.error());
  }
  if (index < gpu_device_time_ns_.size()) {
    breakdown->gpu_device_time_ns = gpu_device_time_ns_[index];
  }
  return breakdown;
}

std::expected<void, diagnostics::Error>
HotPathInstrument::set_gpu_device_time_ns(std::size_t index,
                                          std::uint64_t device_time_ns) {
  const auto recorded = frames_.samples();
  if (index >= recorded.size() || index >= gpu_device_time_ns_.size()) {
    return std::unexpected(
        make_invalid("sample_index_oob", "frame sample index out of range"));
  }
  gpu_device_time_ns_[index] = device_time_ns;
  return {};
}

} // namespace instantee::runtime
