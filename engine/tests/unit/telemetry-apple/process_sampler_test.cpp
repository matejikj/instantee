#include <instantee/telemetry_apple/process_sampler.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <sys/resource.h>
#include <thread>
#include <time.h>
#include <vector>

int main() {
  using namespace instantee::telemetry_apple;
  using clock = std::chrono::steady_clock;
  const auto before = sample_process(0);
  if (!before.user_ns || !before.system_ns || !before.rss_current_bytes ||
      !before.rss_peak_bytes) {
    return 1;
  }
  std::vector<std::uint8_t> memory(32U * 1024U * 1024U);
  volatile std::uint8_t *resident = memory.data();
  for (std::size_t i = 0; i < memory.size(); i += 4096) {
    resident[i] = 42;
  }
  const auto after = sample_process(1);
  rusage usage{};
  if (!after.rss_current_bytes || !after.rss_peak_bytes ||
      getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss <= 0 ||
      *after.rss_current_bytes <
          *before.rss_current_bytes + 16U * 1024U * 1024U) {
    return 2;
  }
  // Corroborate Darwin ru_maxrss byte units against Mach's explicitly
  // byte-sized high-water value. Small growth between calls may raise the later
  // value.
  const auto raw_peak = static_cast<std::uint64_t>(usage.ru_maxrss);
  if (raw_peak < *after.rss_peak_bytes ||
      raw_peak > *after.rss_peak_bytes + 16U * 1024U * 1024U) {
    return 3;
  }
  std::printf("{\"allocation_bytes\":33554432,\"mach_rss_growth_bytes\":%llu,"
              "\"mach_peak_bytes\":%llu,\"getrusage_peak_bytes\":%llu}\n",
              static_cast<unsigned long long>(*after.rss_current_bytes -
                                              *before.rss_current_bytes),
              static_cast<unsigned long long>(*after.rss_peak_bytes),
              static_cast<unsigned long long>(raw_peak));
  const auto cpu_before = sample_process(2);
  std::uint64_t worker_cpu[2]{};
  auto burn = [&](unsigned index) {
    timespec start{}, end{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start) != 0) {
      return;
    }
    std::atomic_uint64_t work{0};
    const auto deadline = clock::now() + std::chrono::milliseconds{50};
    while (clock::now() < deadline) {
      work.fetch_add(1, std::memory_order_relaxed);
    }
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end) != 0) {
      return;
    }
    worker_cpu[index] = static_cast<std::uint64_t>(
        (end.tv_sec - start.tv_sec) * 1000000000 + end.tv_nsec - start.tv_nsec);
  };
  std::jthread first([&] { burn(0); });
  std::jthread second([&] { burn(1); });
  first.join();
  second.join();
  const auto cpu_after = sample_process(3);
  if (!cpu_before.user_ns || !cpu_before.system_ns || !cpu_after.user_ns ||
      !cpu_after.system_ns) {
    return 9;
  }
  const auto process_cpu = (*cpu_after.user_ns - *cpu_before.user_ns) +
                           (*cpu_after.system_ns - *cpu_before.system_ns);
  if (worker_cpu[0] + worker_cpu[1] < 1000000 ||
      process_cpu + 2000000 < worker_cpu[0] + worker_cpu[1]) {
    return 10;
  }
  if (ProcessSampler::start(clock::now(), 0) ||
      ProcessSampler::start(clock::now(), 1, std::chrono::milliseconds{0})) {
    return 4;
  }
  auto sampler =
      ProcessSampler::start(clock::now(), 1, std::chrono::milliseconds{1});
  if (!sampler) {
    return 5;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  (*sampler)->finish();
  (*sampler)->finish();
  if ((*sampler)->samples().size() != 1 || (*sampler)->lost_samples() == 0) {
    return 6;
  }
  auto normal = ProcessSampler::start(clock::now(), 100);
  if (!normal) {
    return 7;
  }
  (*normal)->finish();
  return (*normal)->samples().size() >= 1 && (*normal)->lost_samples() == 0 ? 0
                                                                            : 8;
}
