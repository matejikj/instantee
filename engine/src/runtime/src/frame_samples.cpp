#include <instantee/runtime/frame_samples.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace instantee::runtime {
FrameSamples::FrameSamples(std::vector<FrameSample> storage) noexcept
    : storage_(std::move(storage)) {}

std::expected<FrameSamples, diagnostics::Error>
FrameSamples::create(std::size_t capacity) {
  if (capacity == 0 || capacity > std::vector<FrameSample>{}.max_size()) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input, "invalid_sample_capacity",
        "telemetry", "Sample capacity is invalid"});
  }
  try {
    return FrameSamples{std::vector<FrameSample>(capacity)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::export_failure, "out_of_memory", "telemetry",
        "Cannot allocate frame samples"});
  }
}

bool FrameSamples::record(FrameSample sample) noexcept {
  const std::array times{
      sample.request_ns,      sample.admitted_ns,
      sample.eval_start_ns,   sample.eval_done_ns,
      sample.gpu_submit_ns,   sample.gpu_completed_observed_ns,
      sample.sink_attempt_ns, sample.sink_accepted_ns};
  if (count_ == storage_.size() || sample.frame != count_ ||
      !std::is_sorted(times.begin(), times.end()) ||
      (count_ != 0 &&
       (sample.request_ns < storage_[count_ - 1].request_ns ||
        sample.sink_accepted_ns < storage_[count_ - 1].sink_accepted_ns))) {
    if (lost_ != std::numeric_limits<std::uint64_t>::max()) {
      ++lost_;
    }
    return false;
  }
  storage_[count_++] = sample;
  return true;
}

std::span<const FrameSample> FrameSamples::samples() const noexcept {
  return std::span{storage_}.first(count_);
}

std::optional<LatencySummary> FrameSamples::summary() const {
  if (count_ == 0 || lost_ != 0) {
    return std::nullopt;
  }
  std::vector<std::uint64_t> latency;
  latency.reserve(count_);
  for (const auto &sample : samples()) {
    latency.push_back(sample.sink_accepted_ns - sample.request_ns);
  }
  std::ranges::sort(latency);
  // ceil(N*p/100)-1, without overflowing N*p or using floating point.
  const auto quantile = [&](std::size_t percent) {
    const auto rank =
        (count_ / 100) * percent + ((count_ % 100) * percent + 99) / 100;
    return latency[rank - 1];
  };
  return LatencySummary{count_,       latency.front(), quantile(50),
                        quantile(95), quantile(99),    latency.back()};
}
} // namespace instantee::runtime
