#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/frame_eval_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;
using instantee::diagnostics::Error;
using instantee::diagnostics::ExitCode;
using instantee::runtime::FrameEvalResult;
using instantee::runtime::FrameEvalScheduler;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::runtime;

  // Delayed oldest frame + capacity-1 queue: order preserved, leases ≤ K.
  {
    constexpr std::uint32_t k = 2;
    constexpr std::uint64_t frames = 8;
    std::atomic_bool release_oldest{false};
    std::atomic_uint32_t peak_credits{0};

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
                return std::unexpected(Error{ExitCode::invalid_input,
                                             "cancelled", "test",
                                             "oldest frame cancelled"});
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
          } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
          }
          return {};
        });
    expect(scheduler.has_value(), "create scheduler");
    if (!scheduler) {
      return EXIT_FAILURE;
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
        std::lock_guard lock(received_mutex);
        received.push_back(next->frame_index);
      }
    });

    std::jthread releaser([&] {
      const auto deadline = clock::now() + std::chrono::seconds(2);
      while ((*scheduler)->reorder_occupied() == 0U &&
             clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(15));
      release_oldest.store(true, std::memory_order_release);
    });

    for (std::uint64_t frame = 0; frame < frames; ++frame) {
      auto submitted = (*scheduler)->submit(frame);
      expect(submitted.has_value(), "submit frame");
      if (!submitted) {
        (*scheduler)->abort();
        return EXIT_FAILURE;
      }
      peak_credits.store(std::max(peak_credits.load(std::memory_order_relaxed),
                                  (*scheduler)->credits_in_flight()),
                         std::memory_order_relaxed);
    }
    releaser.join();
    (*scheduler)->close();
    sink.join();

    expect(received.size() == frames, "received all frames");
    for (std::uint64_t index = 0; index < received.size(); ++index) {
      expect(received[static_cast<std::size_t>(index)] == index,
             "output order preserved");
    }
    expect(peak_credits.load() <= k, "peak leases ≤ K");
    expect((*scheduler)->credits_in_flight() == 0U, "credits drained");
  }

  // Capacity-1 queue: cancel blocked push keeps the lease/credit.
  {
    FrameEvalScheduler::Config config;
    config.credit_capacity = 3;
    config.queue_capacity = 1;
    config.worker_count = 1;

    std::atomic_bool block_worker{true};
    auto scheduler = FrameEvalScheduler::create(
        config,
        [&](std::uint64_t, std::stop_token stop) -> std::expected<void, Error> {
          while (block_worker.load(std::memory_order_acquire)) {
            if (stop.stop_requested()) {
              return std::unexpected(Error{ExitCode::invalid_input, "cancelled",
                                           "test", "worker cancelled"});
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          return {};
        });
    expect(scheduler.has_value(), "create keep-lease scheduler");
    if (!scheduler) {
      return EXIT_FAILURE;
    }

    expect((*scheduler)->submit(0).has_value(), "seed frame 0");
    const auto start = clock::now();
    while ((*scheduler)->evaluating() == 0U &&
           clock::now() - start < std::chrono::seconds(2)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect((*scheduler)->evaluating() == 1U, "worker evaluating frame 0");

    auto queued = (*scheduler)->acquire_lease(1);
    expect(queued.has_value(), "lease frame 1");
    if (queued) {
      expect((*scheduler)->submit_lease(*queued).has_value(), "queue frame 1");
    }

    auto blocked = (*scheduler)->acquire_lease(2);
    expect(blocked.has_value(), "lease frame 2");
    expect(blocked && static_cast<bool>(blocked->credit),
           "frame 2 holds credit");

    std::stop_source stop;
    std::expected<void, Error> push_result;
    std::jthread pusher([&] {
      push_result = (*scheduler)->submit_lease(*blocked, stop.get_token());
    });
    while ((*scheduler)->queue_metrics().blocked_push_count == 0U &&
           clock::now() - start < std::chrono::seconds(2)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect((*scheduler)->queue_metrics().blocked_push_count > 0U,
           "push blocked on capacity 1");
    stop.request_stop();
    pusher.join();
    expect(!push_result, "blocked push failed");
    if (!push_result) {
      expect(push_result.error().code == "cancelled", "push cancelled code");
    }
    expect(blocked && static_cast<bool>(blocked->credit),
           "failed push kept lease credit");

    block_worker.store(false, std::memory_order_release);
    (*scheduler)->abort();
  }

  // Cancel on empty take_next wakes without deadlock.
  {
    FrameEvalScheduler::Config config;
    config.credit_capacity = 2;
    config.queue_capacity = 1;
    config.worker_count = 1;
    auto scheduler = FrameEvalScheduler::create(
        config,
        [](std::uint64_t, std::stop_token) -> std::expected<void, Error> {
          return {};
        });
    expect(scheduler.has_value(), "empty cancel scheduler");
    if (!scheduler) {
      return EXIT_FAILURE;
    }

    std::stop_source stop;
    std::expected<FrameEvalResult, Error> take_result;
    std::jthread taker(
        [&] { take_result = (*scheduler)->take_next(stop.get_token()); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.request_stop();
    (*scheduler)->abort();
    taker.join();
    expect(!take_result, "empty take cancelled");
    if (!take_result) {
      expect(take_result.error().code == "cancelled",
             "empty take cancelled code");
    }
  }

  // close drains remaining ordered frames (sink concurrent with producer).
  {
    FrameEvalScheduler::Config config;
    config.credit_capacity = 2;
    config.queue_capacity = 1;
    config.worker_count = 2;
    auto scheduler = FrameEvalScheduler::create(
        config,
        [](std::uint64_t, std::stop_token) -> std::expected<void, Error> {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          return {};
        });
    expect(scheduler.has_value(), "drain scheduler");
    if (!scheduler) {
      return EXIT_FAILURE;
    }

    std::vector<std::uint64_t> got;
    std::mutex got_mutex;
    std::jthread sink([&](std::stop_token stop) {
      for (;;) {
        auto next = (*scheduler)->take_next(stop);
        if (!next) {
          expect(next.error().code == "closed", "drain ends closed");
          break;
        }
        std::lock_guard lock(got_mutex);
        got.push_back(next->frame_index);
      }
    });

    for (std::uint64_t frame = 0; frame < 5; ++frame) {
      expect((*scheduler)->submit(frame).has_value(), "submit drain frame");
    }
    (*scheduler)->close();
    sink.join();

    expect(got.size() == 5, "close drained five frames");
    for (std::uint64_t i = 0; i < got.size(); ++i) {
      expect(got[static_cast<std::size_t>(i)] == i, "drain order");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
