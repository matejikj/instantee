#include <instantee/runtime/frame_credits.hpp>

#include <condition_variable>
#include <mutex>
#include <new>
#include <utility>

namespace instantee::runtime {
namespace {

constexpr const char *kPhase = "frame_credits";

using clock = std::chrono::steady_clock;

} // namespace

struct detail::FrameCreditPoolState {
  std::uint32_t capacity = 0;
  std::uint32_t in_flight = 0;
  bool closed = false;
  PipelineMetrics metrics{};

  std::mutex mutex;
  std::condition_variable_any available;

  void notify_all_unlocked() noexcept { available.notify_all(); }
};

FrameCredit::FrameCredit(
    std::shared_ptr<detail::FrameCreditPoolState> state) noexcept
    : state_(std::move(state)) {}

FrameCredit::FrameCredit(FrameCredit &&other) noexcept
    : state_(std::move(other.state_)) {}

FrameCredit &FrameCredit::operator=(FrameCredit &&other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
  }
  return *this;
}

FrameCredit::~FrameCredit() { release(); }

void FrameCredit::release() noexcept {
  auto state = std::move(state_);
  if (!state) {
    return;
  }
  std::lock_guard lock(state->mutex);
  if (state->in_flight > 0U) {
    --state->in_flight;
  }
  state->notify_all_unlocked();
}

FrameCreditPool::FrameCreditPool(
    std::shared_ptr<detail::FrameCreditPoolState> state) noexcept
    : state_(std::move(state)) {}

FrameCreditPool::FrameCreditPool(FrameCreditPool &&) noexcept = default;
FrameCreditPool &
FrameCreditPool::operator=(FrameCreditPool &&) noexcept = default;

FrameCreditPool::~FrameCreditPool() { close(); }

std::expected<FrameCreditPool, spike::Error>
FrameCreditPool::create(std::uint32_t capacity) {
  if (capacity == 0U) {
    return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                        "invalid_credit_capacity", kPhase,
                                        "Frame credit capacity must be "
                                        "non-zero"});
  }
  auto state = std::shared_ptr<detail::FrameCreditPoolState>(
      new (std::nothrow) detail::FrameCreditPoolState{});
  if (!state) {
    return std::unexpected(spike::Error{spike::ExitCode::export_failure,
                                        "out_of_memory", kPhase,
                                        "Cannot allocate frame credit pool"});
  }
  state->capacity = capacity;
  return FrameCreditPool{std::move(state)};
}

std::uint32_t FrameCreditPool::capacity() const noexcept {
  return state_ ? state_->capacity : 0U;
}

std::uint32_t FrameCreditPool::in_flight() const noexcept {
  if (!state_) {
    return 0U;
  }
  std::lock_guard lock(state_->mutex);
  return state_->in_flight;
}

bool FrameCreditPool::closed() const noexcept {
  return !state_ || state_->closed;
}

PipelineMetrics FrameCreditPool::metrics() const noexcept {
  if (!state_) {
    return {};
  }
  std::lock_guard lock(state_->mutex);
  return state_->metrics;
}

void FrameCreditPool::close() noexcept {
  if (!state_) {
    return;
  }
  std::lock_guard lock(state_->mutex);
  state_->closed = true;
  state_->notify_all_unlocked();
}

std::expected<FrameCredit, spike::Error>
FrameCreditPool::acquire(std::stop_token stop,
                         std::optional<clock::time_point> deadline) {
  if (!state_) {
    return std::unexpected(spike::Error{spike::ExitCode::export_failure,
                                        "credits_not_ready", kPhase,
                                        "Frame credit pool is not ready"});
  }

  std::unique_lock lock(state_->mutex);
  for (;;) {
    if (stop.stop_requested()) {
      return std::unexpected(
          spike::Error{spike::ExitCode::invalid_input, "cancelled", kPhase,
                       "Frame credit acquire was cancelled"});
    }
    if (state_->closed) {
      return std::unexpected(spike::Error{spike::ExitCode::invalid_input,
                                          "closed", kPhase,
                                          "Frame credit pool is closed"});
    }
    if (state_->in_flight < state_->capacity) {
      ++state_->in_flight;
      if (state_->in_flight > state_->metrics.high_water) {
        state_->metrics.high_water = state_->in_flight;
      }
      return FrameCredit{state_};
    }

    ++state_->metrics.blocked_acquire_count;
    const auto started = clock::now();
    const auto wake = [&] {
      return state_->closed || stop.stop_requested() ||
             state_->in_flight < state_->capacity;
    };
    if (deadline.has_value()) {
      if (clock::now() >= *deadline) {
        return std::unexpected(
            spike::Error{spike::ExitCode::invalid_input, "deadline_exceeded",
                         kPhase, "Frame credit acquire deadline exceeded"});
      }
      state_->available.wait_until(lock, stop, *deadline, wake);
      if (!wake() && clock::now() >= *deadline) {
        return std::unexpected(
            spike::Error{spike::ExitCode::invalid_input, "deadline_exceeded",
                         kPhase, "Frame credit acquire deadline exceeded"});
      }
    } else {
      state_->available.wait(lock, stop, wake);
    }
    const auto elapsed = clock::now() - started;
    state_->metrics.blocked_wait_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  }
}

} // namespace instantee::runtime
