#include <instantee/runtime/frame_eval_scheduler.hpp>

#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "frame_eval_scheduler", message};
}

} // namespace

FrameEvalScheduler::FrameEvalScheduler(Config config, EvaluateFn evaluate,
                                       FrameCreditPool credits,
                                       BoundedQueue<FrameEvalLease> work,
                                       ReorderBuffer<FrameEvalResult> reorder)
    : config_(config), evaluate_(std::move(evaluate)),
      credits_(std::move(credits)), work_(std::move(work)),
      reorder_(std::move(reorder)) {
  workers_.reserve(config_.worker_count);
  for (std::uint32_t index = 0; index < config_.worker_count; ++index) {
    workers_.emplace_back([this] { worker_main(); });
  }
}

FrameEvalScheduler::~FrameEvalScheduler() {
  abort();
  workers_.clear();
}

std::expected<std::unique_ptr<FrameEvalScheduler>, diagnostics::Error>
FrameEvalScheduler::create(Config config, EvaluateFn evaluate) {
  if (!evaluate) {
    return std::unexpected(
        err("invalid_input", "Evaluate callback is required"));
  }
  if (config.worker_count == 0U) {
    return std::unexpected(
        err("invalid_input", "Worker count must be non-zero"));
  }
  if (config.credit_capacity == 0U) {
    return std::unexpected(
        err("invalid_input", "Credit capacity must be non-zero"));
  }
  if (config.queue_capacity == 0U) {
    return std::unexpected(
        err("invalid_input", "Queue capacity must be non-zero"));
  }

  auto credits = FrameCreditPool::create(config.credit_capacity);
  if (!credits) {
    return std::unexpected(credits.error());
  }
  auto work = BoundedQueue<FrameEvalLease>::create(config.queue_capacity);
  if (!work) {
    return std::unexpected(work.error());
  }
  auto reorder = ReorderBuffer<FrameEvalResult>::create(config.credit_capacity);
  if (!reorder) {
    return std::unexpected(reorder.error());
  }

  return std::unique_ptr<FrameEvalScheduler>(
      new FrameEvalScheduler(config, std::move(evaluate), std::move(*credits),
                             std::move(*work), std::move(*reorder)));
}

std::uint32_t FrameEvalScheduler::credit_capacity() const noexcept {
  return config_.credit_capacity;
}

std::size_t FrameEvalScheduler::queue_capacity() const noexcept {
  return config_.queue_capacity;
}

std::uint32_t FrameEvalScheduler::worker_count() const noexcept {
  return config_.worker_count;
}

std::uint32_t FrameEvalScheduler::credits_in_flight() const noexcept {
  return credits_.in_flight();
}

std::uint32_t FrameEvalScheduler::reorder_occupied() const noexcept {
  std::lock_guard lock(reorder_mutex_);
  return reorder_.occupied();
}

std::uint32_t FrameEvalScheduler::evaluating() const noexcept {
  return evaluating_.load(std::memory_order_acquire);
}

bool FrameEvalScheduler::stop_requested() const noexcept {
  return stop_source_.stop_requested() ||
         aborted_.load(std::memory_order_acquire);
}

PipelineMetrics FrameEvalScheduler::queue_metrics() const noexcept {
  return work_.metrics();
}

void FrameEvalScheduler::fail_job(diagnostics::Error error) noexcept {
  {
    std::lock_guard lock(error_mutex_);
    if (!first_error_.has_value()) {
      first_error_ = std::move(error);
    }
  }
  request_stop();
}

void FrameEvalScheduler::request_stop() noexcept {
  accepting_.store(false, std::memory_order_release);
  stop_source_.request_stop();
  reorder_cv_.notify_all();
}

void FrameEvalScheduler::close() noexcept {
  accepting_.store(false, std::memory_order_release);
  work_.close();
  reorder_cv_.notify_all();
}

void FrameEvalScheduler::abort() noexcept {
  aborted_.store(true, std::memory_order_release);
  accepting_.store(false, std::memory_order_release);
  stop_source_.request_stop();
  work_.abort();
  credits_.close();
  reorder_cv_.notify_all();
}

std::expected<FrameEvalLease, diagnostics::Error>
FrameEvalScheduler::acquire_lease(std::uint64_t frame_index,
                                  std::stop_token stop) {
  if (!accepting_.load(std::memory_order_acquire) ||
      aborted_.load(std::memory_order_acquire) || stop.stop_requested() ||
      stop_source_.stop_requested()) {
    return std::unexpected(
        err("cancelled", "Frame evaluation scheduler is not accepting work"));
  }

  {
    std::lock_guard lock(producer_mutex_);
    if (frame_index != next_submit_) {
      return std::unexpected(
          err("invalid_input",
              "Producer must submit contiguous ascending frame indices"));
    }
  }

  auto credit = credits_.acquire(stop);
  if (!credit) {
    return std::unexpected(credit.error());
  }

  {
    std::lock_guard lock(producer_mutex_);
    if (frame_index != next_submit_) {
      return std::unexpected(
          err("invalid_input",
              "Producer must submit contiguous ascending frame indices"));
    }
    ++next_submit_;
  }
  return FrameEvalLease{.frame_index = frame_index,
                        .credit = std::move(*credit)};
}

std::expected<void, diagnostics::Error>
FrameEvalScheduler::submit_lease(FrameEvalLease &lease, std::stop_token stop) {
  if (aborted_.load(std::memory_order_acquire) || stop.stop_requested() ||
      stop_source_.stop_requested()) {
    return std::unexpected(
        err("cancelled", "Frame evaluation scheduler submit was cancelled"));
  }

  FrameEvalLease held = std::move(lease);
  auto pushed = work_.push(std::move(held), stop);
  if (!pushed) {
    // BoundedQueue moves the value only on successful insert.
    lease = std::move(held); // NOLINT(bugprone-use-after-move)
    return std::unexpected(pushed.error());
  }
  return {};
}

std::expected<void, diagnostics::Error>
FrameEvalScheduler::submit(std::uint64_t frame_index, std::stop_token stop) {
  auto lease = acquire_lease(frame_index, stop);
  if (!lease) {
    return std::unexpected(lease.error());
  }
  return submit_lease(*lease, stop);
}

void FrameEvalScheduler::publish_completed(FrameEvalResult result) {
  const std::uint64_t frame_index = result.frame_index;
  {
    std::lock_guard lock(reorder_mutex_);
    auto inserted = reorder_.insert(frame_index, std::move(result));
    if (!inserted) {
      fail_job(inserted.error());
      return;
    }
  }
  reorder_cv_.notify_all();
}

void FrameEvalScheduler::worker_main() {
  for (;;) {
    auto item = work_.pop(stop_source_.get_token());
    if (!item) {
      const std::string_view code = item.error().code;
      if (code == "closed" || code == "cancelled") {
        reorder_cv_.notify_all();
        return;
      }
      fail_job(item.error());
      reorder_cv_.notify_all();
      return;
    }

    FrameEvalLease lease = std::move(*item);
    const std::uint64_t frame = lease.frame_index;
    evaluating_.fetch_add(1, std::memory_order_acq_rel);
    auto evaluated = evaluate_(frame, stop_source_.get_token());
    if (!evaluated) {
      evaluating_.fetch_sub(1, std::memory_order_acq_rel);
      fail_job(evaluated.error());
      reorder_cv_.notify_all();
      return;
    }

    FrameEvalResult result{.frame_index = frame,
                           .credit = std::move(lease.credit)};
    publish_completed(std::move(result));
    evaluating_.fetch_sub(1, std::memory_order_acq_rel);
    reorder_cv_.notify_all();
  }
}

std::expected<FrameEvalResult, diagnostics::Error>
FrameEvalScheduler::take_next(std::stop_token stop) {
  std::unique_lock lock(reorder_mutex_);
  for (;;) {
    if (auto next = reorder_.try_pop_next()) {
      return std::move(*next);
    }

    if (aborted_.load(std::memory_order_acquire) || stop.stop_requested() ||
        stop_source_.stop_requested()) {
      std::lock_guard error_lock(error_mutex_);
      if (first_error_.has_value()) {
        return std::unexpected(*first_error_);
      }
      return std::unexpected(
          err("cancelled", "Frame evaluation take_next was cancelled"));
    }

    // Inspect the work queue without holding the reorder mutex to avoid
    // lock-order coupling with producer/worker queue operations.
    lock.unlock();
    const bool drained = !accepting_.load(std::memory_order_acquire) &&
                         work_.closed() && work_.size() == 0U &&
                         evaluating_.load(std::memory_order_acquire) == 0U;
    lock.lock();

    if (auto next = reorder_.try_pop_next()) {
      return std::move(*next);
    }
    if (drained && reorder_.occupied() == 0U) {
      return std::unexpected(
          err("closed", "Frame evaluation scheduler drained"));
    }

    reorder_cv_.wait_for(lock, std::chrono::milliseconds(1), [&] {
      return reorder_.occupied() > 0U ||
             aborted_.load(std::memory_order_acquire) ||
             stop.stop_requested() || stop_source_.stop_requested() ||
             (!accepting_.load(std::memory_order_acquire) && work_.closed());
    });
  }
}

} // namespace instantee::runtime
