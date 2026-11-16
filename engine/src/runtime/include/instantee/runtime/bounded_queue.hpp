#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/pipeline_metrics.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace instantee::runtime {

// Single-producer / multi-waiter FIFO with fixed capacity. Full push and empty
// pop block until space/item, close, cancel, or optional deadline. Closed
// queues reject new pushes and drain remaining items before returning closed on
// pop. Values require noexcept move/destruction; a moved-from value must have
// inert cleanup, since removing its empty shell occurs under the queue mutex.
template <typename T> class BoundedQueue {
public:
  static_assert(std::is_nothrow_move_constructible_v<T> &&
                std::is_nothrow_destructible_v<T>);

  BoundedQueue(const BoundedQueue &) = delete;
  BoundedQueue &operator=(const BoundedQueue &) = delete;
  BoundedQueue(BoundedQueue &&) noexcept = default;
  BoundedQueue &operator=(BoundedQueue &&) noexcept = default;
  ~BoundedQueue() { close(); }

  [[nodiscard]] static std::expected<BoundedQueue, diagnostics::Error>
  create(std::size_t capacity);

  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] bool closed() const noexcept;
  [[nodiscard]] PipelineMetrics metrics() const noexcept;

  // Wakes blocked push/pop. Remaining items stay available until drained.
  void close() noexcept;

  // Rejects new work and retires pending items outside the queue mutex.
  // Idempotent; blocked push/pop wake with cancelled. In-flight work remains
  // owned by its consumer and must be retired before joining that consumer.
  void abort() noexcept;

  // The rvalue reference is consumed only on successful insertion.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  push(T &&value, std::stop_token stop = {},
       std::optional<std::chrono::steady_clock::time_point> deadline =
           std::nullopt);

  [[nodiscard]] std::expected<T, diagnostics::Error>
  pop(std::stop_token stop = {},
      std::optional<std::chrono::steady_clock::time_point> deadline =
          std::nullopt);

private:
  using clock = std::chrono::steady_clock;

  static constexpr const char *kPhase = "bounded_queue";

  struct State {
    explicit State(std::size_t capacity) : capacity(capacity) {}

    const std::size_t capacity;
    std::mutex mutex;
    std::condition_variable_any not_full;
    std::condition_variable_any not_empty;
    std::unique_ptr<std::deque<T>> items = std::make_unique<std::deque<T>>();
    bool aborted = false;
    bool closed = false;
    PipelineMetrics metrics{};
  };

  explicit BoundedQueue(std::unique_ptr<State> state) noexcept
      : state_(std::move(state)) {}

  std::unique_ptr<State> state_{};
};

template <typename T>
std::expected<BoundedQueue<T>, diagnostics::Error>
BoundedQueue<T>::create(std::size_t capacity) {
  if (capacity == 0U) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input, "invalid_queue_capacity", kPhase,
        "Bounded queue capacity must be "
        "non-zero"});
  }
  std::unique_ptr<State> state;
  try {
    state = std::make_unique<State>(capacity);
  } catch (const std::bad_alloc &) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::export_failure, "out_of_memory", kPhase,
        "Cannot allocate bounded queue"});
  }
  return BoundedQueue{std::move(state)};
}

template <typename T> std::size_t BoundedQueue<T>::capacity() const noexcept {
  return state_ ? state_->capacity : 0U;
}

template <typename T> std::size_t BoundedQueue<T>::size() const noexcept {
  if (!state_) {
    return 0U;
  }
  std::lock_guard lock(state_->mutex);
  return state_->items ? state_->items->size() : 0U;
}

template <typename T> bool BoundedQueue<T>::closed() const noexcept {
  if (!state_) {
    return true;
  }
  std::lock_guard lock(state_->mutex);
  return state_->closed;
}

template <typename T>
PipelineMetrics BoundedQueue<T>::metrics() const noexcept {
  if (!state_) {
    return {};
  }
  std::lock_guard lock(state_->mutex);
  return state_->metrics;
}

template <typename T> void BoundedQueue<T>::close() noexcept {
  if (!state_) {
    return;
  }
  std::lock_guard lock(state_->mutex);
  state_->closed = true;
  state_->not_full.notify_all();
  state_->not_empty.notify_all();
}

template <typename T> void BoundedQueue<T>::abort() noexcept {
  if (!state_) {
    return;
  }
  std::unique_ptr<std::deque<T>> retired;
  {
    std::lock_guard lock(state_->mutex);
    state_->aborted = true;
    state_->closed = true;
    retired = std::move(state_->items);
  }
  state_->not_full.notify_all();
  state_->not_empty.notify_all();
}

template <typename T>
std::expected<void, diagnostics::Error>
BoundedQueue<T>::push(T &&value, std::stop_token stop,
                      std::optional<clock::time_point> deadline) {
  if (!state_) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::export_failure, "queue_not_ready", kPhase,
        "Bounded queue is not ready"});
  }
  std::unique_lock lock(state_->mutex);
  for (;;) {
    if (state_->aborted || stop.stop_requested()) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::invalid_input, "cancelled",
                             kPhase, "Bounded queue push was cancelled"});
    }
    if (state_->closed) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::invalid_input, "closed",
                             kPhase, "Bounded queue is closed"});
    }
    if (state_->items->size() < state_->capacity) {
      state_->items->push_back(std::move(value));
      if (state_->items->size() > state_->metrics.high_water) {
        state_->metrics.high_water = state_->items->size();
      }
      state_->not_empty.notify_one();
      return {};
    }

    ++state_->metrics.blocked_push_count;
    const auto started = clock::now();
    const auto wake = [&] {
      return state_->closed || state_->aborted || stop.stop_requested() ||
             state_->items->size() < state_->capacity;
    };
    if (deadline.has_value()) {
      if (clock::now() >= *deadline) {
        return std::unexpected(diagnostics::Error{
            diagnostics::ExitCode::invalid_input, "deadline_exceeded", kPhase,
            "Bounded queue push deadline exceeded"});
      }
      state_->not_full.wait_until(lock, stop, *deadline, wake);
      if (!wake() && clock::now() >= *deadline) {
        return std::unexpected(diagnostics::Error{
            diagnostics::ExitCode::invalid_input, "deadline_exceeded", kPhase,
            "Bounded queue push deadline exceeded"});
      }
    } else {
      state_->not_full.wait(lock, stop, wake);
    }
    const auto elapsed = clock::now() - started;
    state_->metrics.blocked_wait_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  }
}

template <typename T>
std::expected<T, diagnostics::Error>
BoundedQueue<T>::pop(std::stop_token stop,
                     std::optional<clock::time_point> deadline) {
  if (!state_) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::export_failure, "queue_not_ready", kPhase,
        "Bounded queue is not ready"});
  }
  std::unique_lock lock(state_->mutex);
  for (;;) {
    if (state_->aborted || stop.stop_requested()) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::invalid_input, "cancelled",
                             kPhase, "Bounded queue pop was cancelled"});
    }
    if (!state_->items->empty()) {
      T value = std::move(state_->items->front());
      state_->items->pop_front();
      state_->not_full.notify_one();
      return value;
    }
    if (state_->closed) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::invalid_input, "closed",
                             kPhase, "Bounded queue is closed"});
    }
    ++state_->metrics.blocked_pop_count;
    const auto started = clock::now();
    const auto wake = [&] {
      return state_->closed || state_->aborted || stop.stop_requested() ||
             !state_->items->empty();
    };
    if (deadline.has_value()) {
      if (clock::now() >= *deadline) {
        return std::unexpected(diagnostics::Error{
            diagnostics::ExitCode::invalid_input, "deadline_exceeded", kPhase,
            "Bounded queue pop deadline exceeded"});
      }
      state_->not_empty.wait_until(lock, stop, *deadline, wake);
      if (!wake() && clock::now() >= *deadline) {
        return std::unexpected(diagnostics::Error{
            diagnostics::ExitCode::invalid_input, "deadline_exceeded", kPhase,
            "Bounded queue pop deadline exceeded"});
      }
    } else {
      state_->not_empty.wait(lock, stop, wake);
    }
    const auto elapsed = clock::now() - started;
    state_->metrics.blocked_wait_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
  }
}

} // namespace instantee::runtime
