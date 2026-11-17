#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/pipeline_metrics.hpp>
#include <instantee/runtime/reorder_buffer.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

namespace instantee::runtime {

// Move-only admitted frame lease. Credit is acquired before queue push; a
// failed push leaves the lease (and credit) with the caller.
struct FrameEvalLease {
  std::uint64_t frame_index = 0;
  FrameCredit credit{};
};

static_assert(std::is_nothrow_move_constructible_v<FrameEvalLease>);
static_assert(std::is_nothrow_destructible_v<FrameEvalLease>);

// Completed evaluation for a sink/export owner. Destroying the result releases
// the admission credit.
struct FrameEvalResult {
  std::uint64_t frame_index = 0;
  FrameCredit credit{};
};

// CPU evaluation stage (CPP_RUNTIME_CONTRACTS §3): admission credits (K) →
// bounded eval queue → fixed jthread pool → K reorder slots → ordered take.
// Composes BoundedQueue, FrameCreditPool and ReorderBuffer; does not replace
// them. Workers share one stop_source. close() drains; abort() cancels.
class FrameEvalScheduler {
public:
  struct Config {
    // Global in-flight budget including reorder occupancy (ADR-0012 default 2).
    std::uint32_t credit_capacity = 2;
    std::size_t queue_capacity = 1;
    std::uint32_t worker_count = 2;
  };

  // Per-frame CPU work. Must not run under scheduler mutexes.
  using EvaluateFn = std::function<std::expected<void, diagnostics::Error>(
      std::uint64_t frame_index, std::stop_token stop)>;

  FrameEvalScheduler(const FrameEvalScheduler &) = delete;
  FrameEvalScheduler &operator=(const FrameEvalScheduler &) = delete;
  FrameEvalScheduler(FrameEvalScheduler &&) = delete;
  FrameEvalScheduler &operator=(FrameEvalScheduler &&) = delete;
  ~FrameEvalScheduler();

  [[nodiscard]] static std::expected<std::unique_ptr<FrameEvalScheduler>,
                                     diagnostics::Error>
  create(Config config, EvaluateFn evaluate);

  [[nodiscard]] std::uint32_t credit_capacity() const noexcept;
  [[nodiscard]] std::size_t queue_capacity() const noexcept;
  [[nodiscard]] std::uint32_t worker_count() const noexcept;
  [[nodiscard]] std::uint32_t credits_in_flight() const noexcept;
  [[nodiscard]] std::uint32_t reorder_occupied() const noexcept;
  [[nodiscard]] std::uint32_t evaluating() const noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;
  [[nodiscard]] PipelineMetrics queue_metrics() const noexcept;

  // Single-producer API: frame indices must be contiguous from 0.
  [[nodiscard]] std::expected<FrameEvalLease, diagnostics::Error>
  acquire_lease(std::uint64_t frame_index, std::stop_token stop = {});

  // Pushes an acquired lease. On failure the lease is restored to `lease` so
  // the credit remains with the caller (queue moves T&& only on success).
  [[nodiscard]] std::expected<void, diagnostics::Error>
  submit_lease(FrameEvalLease &lease, std::stop_token stop = {});

  // acquire_lease + submit_lease. On push failure the credit is released when
  // the temporary lease is destroyed (use submit_lease to keep it).
  [[nodiscard]] std::expected<void, diagnostics::Error>
  submit(std::uint64_t frame_index, std::stop_token stop = {});

  // Next contiguous completed frame, or closed/cancelled.
  [[nodiscard]] std::expected<FrameEvalResult, diagnostics::Error>
  take_next(std::stop_token stop = {});

  void close() noexcept;
  void abort() noexcept;
  void request_stop() noexcept;

private:
  FrameEvalScheduler(Config config, EvaluateFn evaluate,
                     FrameCreditPool credits, BoundedQueue<FrameEvalLease> work,
                     ReorderBuffer<FrameEvalResult> reorder);

  void worker_main();
  void publish_completed(FrameEvalResult result);
  void fail_job(diagnostics::Error error) noexcept;

  Config config_{};
  EvaluateFn evaluate_;
  FrameCreditPool credits_;
  BoundedQueue<FrameEvalLease> work_;
  ReorderBuffer<FrameEvalResult> reorder_;

  mutable std::mutex reorder_mutex_;
  std::condition_variable_any reorder_cv_;
  std::stop_source stop_source_;
  std::vector<std::jthread> workers_;

  std::mutex producer_mutex_;
  std::uint64_t next_submit_ = 0;
  std::atomic_bool accepting_{true};
  std::atomic_bool aborted_{false};
  std::atomic_uint32_t evaluating_{0};
  std::mutex error_mutex_;
  std::optional<diagnostics::Error> first_error_;
};

} // namespace instantee::runtime
