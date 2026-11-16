#include <instantee/runtime/frame_samples.hpp>
#include <limits>

using instantee::runtime::FrameSample;
using instantee::runtime::FrameSamples;

int main() {
  if (FrameSamples::create(0) ||
      FrameSamples::create(std::numeric_limits<std::size_t>::max())) {
    return 1;
  }
  auto log = FrameSamples::create(100);
  if (!log || log->summary()) {
    return 2;
  }
  for (std::uint64_t i = 0; i < 100; ++i) {
    const auto start = i * 1000;
    // Admission dominates half the latency; the last frame is a tail outlier.
    const auto latency = i == 99 ? 900 : i + 1;
    FrameSample sample{i,
                       start,
                       start + latency / 2,
                       start + latency / 2,
                       start + latency / 2,
                       start + latency / 2,
                       start + latency / 2,
                       start + latency / 2,
                       start + latency};
    if (!log->record(sample)) {
      return 3;
    }
  }
  const auto result = log->summary();
  if (!result || result->sample_count != 100 || result->min != 1 ||
      result->p50 != 50 || result->p95 != 95 || result->p99 != 99 ||
      result->max != 900) {
    return 4;
  }
  if (log->record(log->samples().back()) || log->lost_samples() != 1 ||
      log->summary()) {
    return 5;
  }
  auto bad = FrameSamples::create(3);
  if (!bad || bad->record(FrameSample{1}) || bad->lost_samples() != 1) {
    return 6;
  }
  FrameSample backwards{};
  backwards.admitted_ns = 9;
  if (bad->record(backwards) || bad->lost_samples() != 2 || bad->summary()) {
    return 7;
  }
  auto short_log = FrameSamples::create(3);
  if (!short_log || !short_log->record(FrameSample{0}) ||
      !short_log->record(FrameSample{1, 1, 1, 1, 1, 1, 1, 1, 101}) ||
      !short_log->record(FrameSample{2, 2, 2, 2, 2, 2, 2, 2, 102})) {
    return 8;
  }
  auto short_result = short_log->summary();
  if (!short_result || short_result->min != 0 || short_result->p50 != 100 ||
      short_result->p99 != 100) {
    return 9;
  }
  return 0;
}
