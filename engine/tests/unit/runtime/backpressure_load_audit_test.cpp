#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/frame_eval_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;
using instantee::diagnostics::Error;
using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;
using instantee::runtime::BoundedQueue;
using instantee::runtime::FrameCreditPool;
using instantee::runtime::FrameEvalScheduler;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void check_try_push_non_consuming() {
  auto queue = BoundedQueue<std::unique_ptr<int>>::create(1);
  expect(queue.has_value(), "queue create");
  if (!queue) {
    return;
  }
  auto first = std::make_unique<int>(1);
  expect(queue->try_push(std::move(first)).has_value(), "first try_push");
  expect(!first, "first moved");

  auto second = std::make_unique<int>(2);
  auto blocked = queue->try_push(std::move(second));
  expect(!blocked.has_value(), "full try_push fails");
  expect(blocked.error().kind == ErrorKind::would_block, "would_block kind");
  expect(blocked.error().code == "would_block", "would_block code");
  // Non-consuming failure: caller still owns the lease (T&& unbound on
  // failure).
  const bool retained_full =
      second && *second == 2; // NOLINT(bugprone-use-after-move)
  expect(retained_full, "value retained on WouldBlock");

  auto popped = queue->pop();
  expect(popped.has_value() && *popped && **popped == 1, "pop first");
  expect(queue->try_push(std::move(second)).has_value(), "retry try_push");
  expect(!second,
         "second moved after success"); // NOLINT(bugprone-use-after-move)

  queue->close();
  auto third = std::make_unique<int>(3);
  auto closed = queue->try_push(std::move(third));
  expect(!closed.has_value() && closed.error().code == "closed", "closed");
  const bool retained_closed =
      third && *third == 3; // NOLINT(bugprone-use-after-move)
  expect(retained_closed, "value retained on closed");
}

void check_composed_slow_sink_oldest_frame() {
  // Locked feasibility profile: K=2, evaluation queue capacity 1. Separate E
  // models the encoder pixel-buffer pool; admission before "encode" keeps E
  // independent of K.
  constexpr std::uint32_t k = 2;
  constexpr std::uint32_t e = 2;
  constexpr std::uint64_t frames = 16;

  std::atomic_bool release_oldest{false};
  std::atomic_uint32_t peak_k{0};
  std::atomic_uint32_t peak_e{0};
  std::atomic_uint64_t spin_iters{0};

  FrameEvalScheduler::Config config;
  config.credit_capacity = k;
  config.queue_capacity = 1;
  config.worker_count = 2;

  auto scheduler = FrameEvalScheduler::create(
      config,
      [&](std::uint64_t frame,
          std::stop_token stop) -> std::expected<void, Error> {
        if (frame == 0) {
          while (!release_oldest.load(std::memory_order_acquire)) {
            if (stop.stop_requested()) {
              return std::unexpected(Error{ExitCode::cancelled,
                                           ErrorKind::cancelled, "cancelled",
                                           "audit", "oldest cancelled"});
            }
            // Intentional wait with sleep — not a busy-spin.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
      });
  expect(scheduler.has_value(), "scheduler create");
  if (!scheduler) {
    return;
  }

  auto encoder_slots = FrameCreditPool::create(e);
  expect(encoder_slots.has_value(), "encoder pool E");
  if (!encoder_slots) {
    return;
  }

  std::vector<std::uint64_t> received;
  received.reserve(static_cast<std::size_t>(frames));
  std::mutex received_mutex;

  std::jthread sink([&](std::stop_token stop) {
    for (;;) {
      auto next = (*scheduler)->take_next(stop);
      if (!next) {
        break;
      }
      // Separate E limit: acquire before "encoding", release after slow sink.
      auto slot = encoder_slots->acquire(stop);
      if (!slot) {
        break;
      }
      peak_e.store(std::max(peak_e.load(std::memory_order_relaxed),
                            encoder_slots->in_flight()),
                   std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      {
        std::lock_guard lock(received_mutex);
        received.push_back(next->frame_index);
      }
      // slot releases on destruction
    }
  });

  std::jthread releaser([&] {
    const auto deadline = clock::now() + std::chrono::seconds(3);
    while ((*scheduler)->reorder_occupied() == 0U && clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    release_oldest.store(true, std::memory_order_release);
  });

  for (std::uint64_t frame = 0; frame < frames; ++frame) {
    // Producer blocks on admission/queue rather than spinning.
    const auto before = clock::now();
    auto submitted = (*scheduler)->submit(frame);
    const auto waited = clock::now() - before;
    expect(submitted.has_value(), "submit");
    if (!submitted) {
      (*scheduler)->abort();
      return;
    }
    if (waited > std::chrono::milliseconds(1)) {
      // Count observed backpressure waits (credit or queue).
      spin_iters.fetch_add(1, std::memory_order_relaxed);
    }
    peak_k.store(std::max(peak_k.load(std::memory_order_relaxed),
                          (*scheduler)->credits_in_flight()),
                 std::memory_order_relaxed);
  }

  releaser.join();
  (*scheduler)->close();
  sink.join();

  expect(received.size() == frames, "no dropped frames");
  for (std::uint64_t i = 0; i < received.size(); ++i) {
    expect(received[static_cast<std::size_t>(i)] == i, "ordered export");
  }
  expect(peak_k.load() <= k, "K plateau");
  expect(peak_e.load() <= e, "E plateau");
  expect((*scheduler)->credits_in_flight() == 0U, "K drained");
  expect(encoder_slots->in_flight() == 0U, "E drained");
  // Slow sink + oldest-frame delay must produce blocking waits, not spin-only.
  expect((*scheduler)->queue_metrics().blocked_wait_ns > 0 ||
             encoder_slots->metrics().blocked_wait_ns > 0 ||
             spin_iters.load() > 0,
         "observed backpressure wait");
  expect((*scheduler)->queue_metrics().high_water <= 1U, "queue cap 1");
}

void check_failed_push_preserves_lease_under_load() {
  FrameEvalScheduler::Config config;
  config.credit_capacity = 3;
  config.queue_capacity = 1;
  config.worker_count = 1;

  std::atomic_bool hold{true};
  auto scheduler = FrameEvalScheduler::create(
      config,
      [&](std::uint64_t, std::stop_token stop) -> std::expected<void, Error> {
        while (hold.load(std::memory_order_acquire) && !stop.stop_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
      });
  expect(scheduler.has_value(), "scheduler");
  if (!scheduler) {
    return;
  }

  // Worker pops frame 0 and holds; frame 1 occupies the capacity-1 queue.
  expect((*scheduler)->submit(0).has_value(), "submit 0");
  const auto fill_deadline = clock::now() + std::chrono::seconds(2);
  while ((*scheduler)->evaluating() == 0U && clock::now() < fill_deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  expect((*scheduler)->submit(1).has_value(), "submit 1 fills queue");

  auto lease = (*scheduler)->acquire_lease(2);
  expect(lease.has_value(), "lease 2");
  if (!lease) {
    (*scheduler)->abort();
    return;
  }

  std::stop_source stop;
  std::jthread canceller([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    stop.request_stop();
  });

  auto pushed = (*scheduler)->submit_lease(*lease, stop.get_token());
  expect(!pushed.has_value(), "push cancelled while queue full");
  expect(pushed.error().code == "cancelled", "cancelled push code");
  // Non-consuming failed push: credit remains with the caller lease.
  expect(static_cast<bool>(lease->credit), "lease credit retained");
  expect((*scheduler)->credits_in_flight() >= 1U, "credit still accounted");

  hold.store(false, std::memory_order_release);
  (*scheduler)->abort();
  canceller.join();
}

} // namespace

int main() {
  check_try_push_non_consuming();
  check_composed_slow_sink_oldest_frame();
  check_failed_push_preserves_lease_under_load();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
