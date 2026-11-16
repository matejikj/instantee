#include <instantee/telemetry_apple/process_sampler.hpp>

#include <condition_variable>
#include <limits>
#include <mach/mach.h>
#include <mutex>
#include <sys/resource.h>
#include <thread>
#include <vector>

namespace instantee::telemetry_apple {
namespace {
std::optional<std::uint64_t> nanoseconds(timeval time) noexcept {
  constexpr auto limit = std::numeric_limits<std::uint64_t>::max();
  if (time.tv_sec < 0 || time.tv_usec < 0 || time.tv_usec >= 1000000 ||
      static_cast<std::uint64_t>(time.tv_sec) >
          (limit - 999999000) / 1000000000) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(time.tv_sec) * 1000000000 +
         static_cast<std::uint64_t>(time.tv_usec) * 1000;
}
} // namespace

ResourceSample sample_process(std::uint64_t observed_ns) noexcept {
  ResourceSample sample;
  sample.observed_ns = observed_ns;
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    sample.user_ns = nanoseconds(usage.ru_utime);
    sample.system_ns = nanoseconds(usage.ru_stime);
  }
  mach_task_basic_info_data_t basic{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&basic),
                &count) == KERN_SUCCESS &&
      count == MACH_TASK_BASIC_INFO_COUNT) {
    sample.rss_current_bytes = basic.resident_size;
    sample.rss_peak_bytes = basic.resident_size_max;
  }
  task_vm_info_data_t vm{};
  count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS &&
      count >= TASK_VM_INFO_REV1_COUNT) {
    sample.physical_footprint_bytes = vm.phys_footprint;
  }
  return sample;
}

struct ProcessSampler::Impl {
  std::chrono::steady_clock::time_point epoch;
  std::chrono::milliseconds period;
  std::vector<ResourceSample> storage;
  std::size_t count = 0;
  std::uint64_t lost = 0;
  std::mutex mutex;
  std::condition_variable_any wake;
  std::jthread worker;

  void capture() noexcept {
    const auto time = std::chrono::steady_clock::now() - epoch;
    if (count == storage.size()) {
      if (lost != std::numeric_limits<std::uint64_t>::max()) {
        ++lost;
      }
      return;
    }
    storage[count++] = sample_process(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(time).count()));
  }
  void run(std::stop_token stop) noexcept {
    try {
      auto next = std::chrono::steady_clock::now();
      capture();
      std::unique_lock lock(mutex);
      while (!stop.stop_requested()) {
        next += period;
        wake.wait_until(lock, stop, next, [] { return false; });
        if (stop.stop_requested()) {
          break;
        }
        capture();
        const auto now = std::chrono::steady_clock::now();
        if (now >= next + period) {
          const auto missed = static_cast<std::uint64_t>((now - next) / period);
          lost = missed > std::numeric_limits<std::uint64_t>::max() - lost
                     ? std::numeric_limits<std::uint64_t>::max()
                     : lost + missed;
          next = now;
        }
      }
      capture();
    } catch (...) {
      lost = std::numeric_limits<std::uint64_t>::max();
    }
  }
};

ProcessSampler::ProcessSampler(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
ProcessSampler::~ProcessSampler() { finish(); }

std::expected<std::unique_ptr<ProcessSampler>, diagnostics::Error>
ProcessSampler::start(std::chrono::steady_clock::time_point epoch,
                      std::size_t capacity, std::chrono::milliseconds period) {
  if (capacity == 0 || capacity > 65536 || period.count() <= 0 ||
      period > std::chrono::seconds{60} ||
      (epoch > std::chrono::steady_clock::now() ||
       epoch < std::chrono::steady_clock::now() - std::chrono::hours{24})) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input, "invalid_sampler_config",
        "telemetry", "Sampler configuration is invalid"});
  }
  try {
    auto impl = std::make_unique<Impl>();
    impl->epoch = epoch;
    impl->period = period;
    impl->storage.resize(capacity);
    impl->worker = std::jthread(
        [state = impl.get()](std::stop_token stop) { state->run(stop); });
    return std::unique_ptr<ProcessSampler>(new ProcessSampler(std::move(impl)));
  } catch (...) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::export_failure, "sampler_start_failed",
        "telemetry", "Cannot start process sampler"});
  }
}
void ProcessSampler::finish() noexcept {
  if (impl_->worker.joinable()) {
    impl_->worker.request_stop();
    impl_->worker.join();
  }
}
std::span<const ResourceSample> ProcessSampler::samples() const noexcept {
  return std::span{impl_->storage}.first(impl_->count);
}
std::uint64_t ProcessSampler::lost_samples() const noexcept {
  return impl_->lost;
}
} // namespace instantee::telemetry_apple
