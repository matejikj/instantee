#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/frame_eval_scheduler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "backpressure shutdown audit must remain free of Metal/Apple headers"
#endif

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

struct LoadReport {
  std::string profile;
  std::uint32_t k = 0;
  std::uint32_t e = 0;
  std::uint64_t frames = 0;
  std::uint32_t peak_k = 0;
  std::uint32_t peak_e = 0;
  std::uint64_t queue_high_water = 0;
  std::uint64_t blocked_wait_ns = 0;
  std::uint64_t delivered = 0;
  std::uint64_t accounted_peak_bytes = 0;
  bool ordered = false;
  bool no_spin_only = false;
};

std::vector<LoadReport> g_load_reports;

void write_overload_report(const std::filesystem::path &path) {
  std::ostringstream out;
  out << "{\n"
      << "  \"schema_version\": 1,\n"
      << "  \"subject\": \"backpressure and shutdown without deadlock\",\n"
      << "  \"measurement\": \"none\",\n"
      << "  \"certified_performance\": false,\n"
      << "  \"backend\": \"cpu-fake\",\n"
      << "  \"scenarios\": {\n"
      << "    \"native_timeouts\": true,\n"
      << "    \"full_empty_stop\": true,\n"
      << "    \"close_drain_abort\": true,\n"
      << "    \"spurious_wakeup_predicate_storm\": true,\n"
      << "    \"controlled_load_k2_k3\": true\n"
      << "  },\n"
      << "  \"loads\": [\n";
  for (std::size_t i = 0; i < g_load_reports.size(); ++i) {
    const auto &r = g_load_reports[i];
    out << "    {\n"
        << "      \"profile\": \"" << r.profile << "\",\n"
        << "      \"k\": " << r.k << ",\n"
        << "      \"e\": " << r.e << ",\n"
        << "      \"frames\": " << r.frames << ",\n"
        << "      \"peak_k\": " << r.peak_k << ",\n"
        << "      \"peak_e\": " << r.peak_e << ",\n"
        << "      \"queue_high_water\": " << r.queue_high_water << ",\n"
        << "      \"blocked_wait_ns\": " << r.blocked_wait_ns << ",\n"
        << "      \"delivered\": " << r.delivered << ",\n"
        << "      \"accounted_peak_bytes\": " << r.accounted_peak_bytes << ",\n"
        << "      \"ordered\": " << (r.ordered ? "true" : "false") << ",\n"
        << "      \"no_spin_only\": " << (r.no_spin_only ? "true" : "false")
        << ",\n"
        << "      \"dropped_frames\": "
        << (r.frames > r.delivered ? (r.frames - r.delivered) : 0) << "\n"
        << "    }" << (i + 1 < g_load_reports.size() ? "," : "") << "\n";
  }
  out << "  ]\n"
      << "}\n";
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << out.str();
  expect(static_cast<bool>(file), "wrote overload report");
}

void audit_native_timeouts() {
  auto queue = BoundedQueue<int>::create(1);
  expect(queue.has_value(), "timeout queue");
  if (!queue) {
    return;
  }
  expect(queue->push(1).has_value(), "fill");
  const auto past = clock::now() - std::chrono::milliseconds(1);
  auto push_timed = queue->push(2, {}, past);
  expect(!push_timed.has_value() &&
             push_timed.error().code == "deadline_exceeded",
         "full push deadline");

  auto popped = queue->pop();
  expect(popped.has_value() && *popped == 1, "drain for empty timeout");
  auto pop_timed = queue->pop({}, past);
  expect(!pop_timed.has_value() &&
             pop_timed.error().code == "deadline_exceeded",
         "empty pop deadline");

  auto credits = FrameCreditPool::create(1);
  expect(credits.has_value(), "credit pool");
  if (!credits) {
    return;
  }
  auto held = credits->acquire();
  expect(held.has_value(), "hold credit");
  auto credit_timed = credits->acquire({}, past);
  expect(!credit_timed.has_value() &&
             credit_timed.error().code == "deadline_exceeded",
         "credit acquire deadline");
}

void audit_full_and_empty_stop() {
  auto queue = BoundedQueue<int>::create(1);
  expect(queue.has_value(), "stop queue");
  if (!queue) {
    return;
  }
  expect(queue->push(7).has_value(), "seed full");

  std::stop_source full_stop;
  std::atomic_bool started{false};
  std::expected<void, Error> push_result;
  std::thread producer([&] {
    started.store(true, std::memory_order_release);
    push_result = queue->push(8, full_stop.get_token());
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  full_stop.request_stop();
  producer.join();
  expect(!push_result.has_value() && push_result.error().code == "cancelled",
         "full push cancelled");
  expect(queue->metrics().blocked_push_count > 0U, "blocked push counted");

  auto first = queue->pop();
  expect(first.has_value() && *first == 7, "pop seed");

  started.store(false, std::memory_order_release);
  std::stop_source empty_stop;
  std::expected<int, Error> pop_result;
  std::thread consumer([&] {
    started.store(true, std::memory_order_release);
    pop_result = queue->pop(empty_stop.get_token());
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  empty_stop.request_stop();
  consumer.join();
  expect(!pop_result.has_value() && pop_result.error().code == "cancelled",
         "empty pop cancelled");
  expect(queue->metrics().blocked_pop_count > 0U, "blocked pop counted");
}

void audit_close_drain_and_abort() {
  auto drain_queue = BoundedQueue<int>::create(2);
  expect(drain_queue.has_value(), "drain queue");
  if (!drain_queue) {
    return;
  }
  expect(drain_queue->push(1).has_value() && drain_queue->push(2).has_value(),
         "fill drain");
  drain_queue->close();
  expect(!drain_queue->push(3).has_value(), "closed rejects push");
  auto a = drain_queue->pop();
  auto b = drain_queue->pop();
  auto c = drain_queue->pop();
  expect(a.has_value() && *a == 1 && b.has_value() && *b == 2, "drain order");
  expect(!c.has_value() && c.error().code == "closed", "drain ends closed");

  auto abort_queue = BoundedQueue<int>::create(1);
  expect(abort_queue.has_value(), "abort queue");
  if (!abort_queue) {
    return;
  }
  expect(abort_queue->push(9).has_value(), "abort seed");
  std::atomic_bool started{false};
  std::expected<void, Error> blocked_push;
  std::thread waiter([&] {
    started.store(true, std::memory_order_release);
    blocked_push = abort_queue->push(10);
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  abort_queue->abort();
  waiter.join();
  expect(!blocked_push.has_value() &&
             (blocked_push.error().code == "cancelled" ||
              blocked_push.error().code == "closed"),
         "abort wakes full push");
  auto after = abort_queue->pop();
  expect(!after.has_value(), "abort retires pending");
}

void audit_spurious_wakeup_predicate_storm() {
  // Predicate loops must tolerate notify storms (abort/close) without hang or
  // ownership loss. This is the spurious-wakeup safety proof for capacity-1
  // queues under concurrent waiters.
  constexpr int rounds = 32;
  for (int round = 0; round < rounds; ++round) {
    auto queue = BoundedQueue<int>::create(1);
    expect(queue.has_value(), "storm queue");
    if (!queue) {
      return;
    }
    std::atomic_bool producer_started{false};
    std::atomic_bool consumer_started{false};
    std::expected<void, Error> push_result;
    std::expected<int, Error> pop_result;

    std::thread producer([&] {
      producer_started.store(true, std::memory_order_release);
      push_result = queue->push(int{round});
    });
    std::thread consumer([&] {
      consumer_started.store(true, std::memory_order_release);
      pop_result = queue->pop();
    });
    while (!producer_started.load(std::memory_order_acquire) ||
           !consumer_started.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if ((round % 2) == 0) {
      queue->abort();
    } else {
      queue->close();
    }
    producer.join();
    consumer.join();
    // Both waiters must exit; abort retires pending, close may deliver or end.
    const bool producer_done = push_result.has_value() ||
                               push_result.error().code == "cancelled" ||
                               push_result.error().code == "closed";
    const bool consumer_done = pop_result.has_value() ||
                               pop_result.error().code == "cancelled" ||
                               pop_result.error().code == "closed";
    expect(producer_done, "producer exited after notify storm");
    expect(consumer_done, "consumer exited after notify storm");
  }
}

void audit_controlled_load(std::uint32_t k, std::uint32_t e,
                           std::uint64_t frames, std::string profile) {
  std::atomic_bool release_oldest{false};
  std::atomic_uint32_t peak_k{0};
  std::atomic_uint32_t peak_e{0};
  std::atomic_uint64_t accounted_peak{0};
  std::atomic_uint64_t accounted_current{0};
  constexpr std::uint64_t frame_bytes = 4096;

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
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {};
      });
  expect(scheduler.has_value(), (profile + " scheduler").c_str());
  if (!scheduler) {
    return;
  }

  auto encoder_slots = FrameCreditPool::create(e);
  expect(encoder_slots.has_value(), (profile + " encoder E").c_str());
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
      auto slot = encoder_slots->acquire(stop);
      if (!slot) {
        break;
      }
      const auto in_e = encoder_slots->in_flight();
      peak_e.store(std::max(peak_e.load(std::memory_order_relaxed), in_e),
                   std::memory_order_relaxed);
      const auto current =
          accounted_current.fetch_add(frame_bytes, std::memory_order_relaxed) +
          frame_bytes;
      accounted_peak.store(
          std::max(accounted_peak.load(std::memory_order_relaxed), current),
          std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      {
        std::lock_guard lock(received_mutex);
        received.push_back(next->frame_index);
      }
      accounted_current.fetch_sub(frame_bytes, std::memory_order_relaxed);
    }
  });

  std::jthread releaser([&] {
    const auto deadline = clock::now() + std::chrono::seconds(3);
    while ((*scheduler)->reorder_occupied() == 0U && clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    release_oldest.store(true, std::memory_order_release);
  });

  for (std::uint64_t frame = 0; frame < frames; ++frame) {
    auto submitted = (*scheduler)->submit(frame);
    expect(submitted.has_value(), (profile + " submit").c_str());
    if (!submitted) {
      (*scheduler)->abort();
      return;
    }
    peak_k.store(std::max(peak_k.load(std::memory_order_relaxed),
                          (*scheduler)->credits_in_flight()),
                 std::memory_order_relaxed);
  }

  releaser.join();
  (*scheduler)->close();
  sink.join();

  bool ordered = received.size() == frames;
  for (std::uint64_t i = 0; i < received.size(); ++i) {
    ordered = ordered && received[static_cast<std::size_t>(i)] == i;
  }
  expect(ordered, (profile + " ordered no drops").c_str());
  expect(peak_k.load() <= k, (profile + " K plateau").c_str());
  expect(peak_e.load() <= e, (profile + " E plateau").c_str());
  expect((*scheduler)->queue_metrics().high_water <= 1U,
         (profile + " queue cap 1").c_str());
  expect(accounted_peak.load() <= static_cast<std::uint64_t>(e) * frame_bytes,
         (profile + " memory plateau").c_str());
  const auto blocked = (*scheduler)->queue_metrics().blocked_wait_ns +
                       encoder_slots->metrics().blocked_wait_ns;
  expect(blocked > 0U, (profile + " observed wait not spin-only").c_str());
  expect((*scheduler)->credits_in_flight() == 0U,
         (profile + " K drained").c_str());
  expect(encoder_slots->in_flight() == 0U, (profile + " E drained").c_str());

  LoadReport report;
  report.profile = std::move(profile);
  report.k = k;
  report.e = e;
  report.frames = frames;
  report.peak_k = peak_k.load();
  report.peak_e = peak_e.load();
  report.queue_high_water = (*scheduler)->queue_metrics().high_water;
  report.blocked_wait_ns = blocked;
  report.delivered = received.size();
  report.accounted_peak_bytes = accounted_peak.load();
  report.ordered = ordered;
  report.no_spin_only = blocked > 0U;
  g_load_reports.push_back(std::move(report));
}

std::optional<std::filesystem::path> parse_report_path(int argc, char **argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string_view(argv[i]) == "--report") {
      return std::filesystem::path{argv[i + 1]};
    }
  }
  return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
  audit_native_timeouts();
  audit_full_and_empty_stop();
  audit_close_drain_and_abort();
  audit_spurious_wakeup_predicate_storm();
  audit_controlled_load(2, 2, 20, "k2-queue1-slow-sink-oldest");
  audit_controlled_load(3, 2, 24, "k3-queue1-slow-sink-oldest");

  if (const auto report = parse_report_path(argc, argv); report.has_value()) {
    write_overload_report(*report);
  }

  if (failures != 0) {
    std::cerr << failures << " backpressure shutdown assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "backpressure_shutdown_audit_test: ok\n";
  return EXIT_SUCCESS;
}
