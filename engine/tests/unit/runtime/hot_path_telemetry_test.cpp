#include <instantee/runtime/hot_path_telemetry.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using namespace instantee::runtime;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(double actual, double expected, double tol) {
  return std::fabs(actual - expected) <= tol;
}

[[nodiscard]] FrameSample make_sample(std::uint64_t frame, std::uint64_t base,
                                      std::uint64_t admit_wait,
                                      std::uint64_t eval,
                                      std::uint64_t gpu_host,
                                      std::uint64_t sink_wait) {
  FrameSample s{};
  s.frame = frame;
  s.request_ns = base;
  s.admitted_ns = base + admit_wait;
  s.eval_start_ns = s.admitted_ns;
  s.eval_done_ns = s.eval_start_ns + eval;
  s.gpu_submit_ns = s.eval_done_ns;
  s.gpu_completed_observed_ns = s.gpu_submit_ns + gpu_host;
  s.sink_attempt_ns = s.gpu_completed_observed_ns;
  s.sink_accepted_ns = s.sink_attempt_ns + sink_wait;
  return s;
}

} // namespace

int main() {
  // Exact nearest-rank oracle on a known series.
  {
    std::vector<std::uint64_t> values;
    for (std::uint64_t i = 1; i <= 100; ++i) {
      values.push_back(i);
    }
    auto summary = exact_nearest_rank(values, 0);
    expect(summary.has_value(), "rank summary");
    if (summary) {
      expect(summary->min == 1 && summary->p50 == 50 && summary->p95 == 95 &&
                 summary->p99 == 99 && summary->max == 100,
             "known p99 oracle");
    }
    expect(!exact_nearest_rank(values, 1).has_value(), "lost invalidates");
    expect(!exact_nearest_rank({}, 0).has_value(), "empty null");
  }

  // CPU totals match BENCHMARK_PROTOCOL formulas.
  {
    auto cpu = compute_cpu_totals(/*user*/ 1'000'000'000ULL,
                                  /*system*/ 500'000'000ULL,
                                  /*wall*/ 2'000'000'000ULL,
                                  /*cpus*/ 8, /*frames*/ 100);
    expect(cpu.has_value(), "cpu totals");
    if (cpu) {
      expect(cpu->cpu_ns == 1'500'000'000ULL, "cpu_ns sum");
      expect(near(cpu->cpu_seconds, 1.5, 1e-12), "cpu_seconds");
      expect(near(cpu->cpu_core_equivalents, 0.75, 1e-12), "core eq");
      expect(near(cpu->cpu_percent_one_core, 75.0, 1e-12), "one-core %");
      expect(near(cpu->cpu_percent_machine, 9.375, 1e-12), "machine %");
      expect(cpu->cpu_seconds_per_verified_frame.has_value() &&
                 near(*cpu->cpu_seconds_per_verified_frame, 0.015, 1e-12),
             "per frame");
      expect(cpu->cpu_scope == kCpuScopeRendererAllThreads, "cpu scope");
    }
    expect(!compute_cpu_totals(1, 1, 0, 8, 1), "reject zero wall");
    expect(!compute_cpu_totals(1, 1, 100, 0, 1), "reject zero cpus");
    expect(!compute_cpu_totals(std::numeric_limits<std::uint64_t>::max(), 1,
                               100, 8, 1),
           "reject cpu counter overflow");
  }

  // Memory peaks: null stays null; peaks track max.
  {
    std::vector<MemoryObservation> obs{
        MemoryObservation{.rss_current_bytes = 100,
                          .rss_peak_bytes = 120,
                          .physical_footprint_bytes = 200},
        MemoryObservation{.rss_current_bytes = 110,
                          .rss_peak_bytes = 150,
                          .physical_footprint_bytes = std::nullopt},
        MemoryObservation{.rss_current_bytes = 90,
                          .rss_peak_bytes = 140,
                          .physical_footprint_bytes = 250},
    };
    const MemoryPeaks peaks = compute_memory_peaks(obs);
    expect(peaks.rss_peak_bytes == 150, "rss peak");
    expect(peaks.physical_footprint_peak_bytes == 250, "footprint peak");

    const MemoryPeaks empty = compute_memory_peaks({});
    expect(!empty.rss_peak_bytes.has_value() &&
               !empty.physical_footprint_peak_bytes.has_value(),
           "missing memory stays null");
  }

  // Hot path: admission/gpu host summaries + GPU host ≠ device time.
  {
    auto instrument = HotPathInstrument::create(100);
    expect(instrument.has_value(), "create instrument");
    if (instrument) {
      for (std::uint64_t i = 0; i < 100; ++i) {
        // Admission wait = i+1 for most frames; last is a tail outlier.
        const std::uint64_t admit = i == 99 ? 900 : i + 1;
        const std::uint64_t gpu_host = (i + 1) * 10;
        auto sample =
            make_sample(i, i * 10'000, admit, /*eval*/ 5, gpu_host, /*sink*/ 3);
        expect(instrument->record(sample), "record");
      }

      const auto frame = instrument->frame_latency_summary();
      expect(frame.has_value(), "frame latency summary");
      const auto admit = instrument->admission_wait_summary();
      expect(admit.has_value(), "admission summary");
      if (admit) {
        expect(admit->min == 1 && admit->p50 == 50 && admit->p95 == 95 &&
                   admit->p99 == 99 && admit->max == 900,
               "admission p99 oracle");
      }
      const auto gpu = instrument->gpu_host_observed_summary();
      expect(gpu.has_value(), "gpu host summary");
      if (gpu) {
        expect(gpu->min == 10 && gpu->p99 == 990 && gpu->max == 1000,
               "gpu host observed p99");
      }

      auto mid = instrument->breakdown_at(49);
      expect(mid.has_value(), "breakdown");
      if (mid) {
        expect(mid->gpu_host_timing_kind ==
                   GpuTimingKind::host_completion_observed,
               "host kind");
        expect(!mid->gpu_device_time_ns.has_value(),
               "host notify is not device time");
        expect(mid->gpu_host_observed_ns == 500, "host gap");
        expect(mid->admission_wait_ns == 50, "admit wait");
      }

      // Explicit device counter may be attached; still distinct from host.
      expect(instrument->set_gpu_device_time_ns(49, 123).has_value(),
             "set device");
      auto with_device = instrument->breakdown_at(49);
      expect(with_device.has_value() &&
                 with_device->gpu_device_time_ns == 123 &&
                 with_device->gpu_host_observed_ns == 500,
             "device and host remain separate");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "hot_path_telemetry_test ok\n";
  return EXIT_SUCCESS;
}
