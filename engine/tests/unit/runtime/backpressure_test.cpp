#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/reorder_buffer.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;
using instantee::runtime::BoundedQueue;
using instantee::runtime::FrameCredit;
using instantee::runtime::FrameCreditPool;
using instantee::runtime::kDefaultFrameCreditCapacity;
using instantee::runtime::ReorderBuffer;

struct AdmittedFrame {
  int index = 0;
  FrameCredit credit{};
};

[[nodiscard]] int fail(int code) { return code; }

[[nodiscard]] int check_credit_capacity_and_release() {
  auto pool = FrameCreditPool::create(2);
  if (!pool || pool->capacity() != 2U) {
    return fail(1);
  }
  auto a = pool->acquire();
  auto b = pool->acquire();
  if (!a || !b || pool->in_flight() != 2U) {
    return fail(2);
  }
  const auto deadline = clock::now() + std::chrono::milliseconds(40);
  auto blocked = pool->acquire({}, deadline);
  if (blocked || blocked.error().code != "deadline_exceeded" ||
      pool->metrics().blocked_acquire_count == 0U) {
    return fail(3);
  }
  a->release();
  auto c = pool->acquire();
  if (!c || pool->in_flight() != 2U || pool->metrics().high_water != 2U) {
    return fail(4);
  }
  return 0;
}

[[nodiscard]] int check_credit_close_and_cancel() {
  auto pool = FrameCreditPool::create(1);
  if (!pool) {
    return fail(5);
  }
  auto held = pool->acquire();
  if (!held) {
    return fail(6);
  }

  std::atomic_bool started{false};
  std::atomic_bool finished{false};
  std::expected<instantee::runtime::FrameCredit, instantee::diagnostics::Error>
      result;
  std::thread waiter([&] {
    started.store(true, std::memory_order_release);
    result = pool->acquire();
    finished.store(true, std::memory_order_release);
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  pool->close();
  waiter.join();
  if (!finished.load(std::memory_order_acquire) || result ||
      result.error().code != "closed") {
    return fail(7);
  }

  auto pool2 = FrameCreditPool::create(1);
  if (!pool2) {
    return fail(8);
  }
  auto held2 = pool2->acquire();
  if (!held2) {
    return fail(9);
  }
  std::stop_source stop;
  started.store(false, std::memory_order_release);
  finished.store(false, std::memory_order_release);
  std::thread cancel_waiter([&] {
    started.store(true, std::memory_order_release);
    result = pool2->acquire(stop.get_token());
    finished.store(true, std::memory_order_release);
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop.request_stop();
  cancel_waiter.join();
  if (!finished.load(std::memory_order_acquire) || result ||
      result.error().code != "cancelled") {
    return fail(10);
  }
  return 0;
}

[[nodiscard]] int check_queue_capacity_one_and_stop() {
  auto queue = BoundedQueue<int>::create(1);
  if (!queue) {
    return fail(11);
  }
  if (!queue->push(7)) {
    return fail(12);
  }

  std::stop_source stop;
  std::atomic_bool started{false};
  std::expected<void, instantee::diagnostics::Error> push_result;
  std::thread producer([&] {
    started.store(true, std::memory_order_release);
    push_result = queue->push(8, stop.get_token());
  });
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stop.request_stop();
  producer.join();
  if (push_result || push_result.error().code != "cancelled" ||
      queue->size() != 1U || queue->metrics().blocked_push_count == 0U) {
    return fail(13);
  }

  auto first = queue->pop();
  if (!first || *first != 7) {
    return fail(14);
  }

  started.store(false, std::memory_order_release);
  std::expected<int, instantee::diagnostics::Error> pop_result;
  std::stop_source empty_stop;
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
  if (pop_result || pop_result.error().code != "cancelled" ||
      queue->metrics().blocked_pop_count == 0U) {
    return fail(15);
  }
  return 0;
}

[[nodiscard]] int check_queue_close_drains() {
  auto queue = BoundedQueue<int>::create(2);
  if (!queue || !queue->push(1) || !queue->push(2)) {
    return fail(16);
  }
  queue->close();
  if (queue->push(3)) {
    return fail(17);
  }
  auto a = queue->pop();
  auto b = queue->pop();
  auto c = queue->pop();
  if (!a || *a != 1 || !b || *b != 2 || c || c.error().code != "closed") {
    return fail(18);
  }
  return 0;
}

// Slow sink + global credits: outstanding work stays within K and queue
// capacity; no busy-spin (blocked wait accumulates) and no frame drops.
// Encoder pixel-buffer limit E stays outside this module (Apple pool).
[[nodiscard]] int check_slow_sink_respects_k() {
  constexpr std::uint32_t k = 3;
  constexpr std::size_t queue_cap = 3;
  constexpr int frames = 24;

  auto credits = FrameCreditPool::create(k);
  auto queue = BoundedQueue<AdmittedFrame>::create(queue_cap);
  if (!credits || !queue) {
    return fail(19);
  }

  std::atomic_uint32_t peak_in_flight{0};
  std::atomic_int produced{0};
  std::atomic_int consumed{0};
  std::vector<int> received;
  received.reserve(static_cast<std::size_t>(frames));

  std::jthread consumer([&](std::stop_token stop) {
    while (consumed.load(std::memory_order_acquire) < frames) {
      auto item = queue->pop(stop);
      if (!item) {
        if (item.error().code == "cancelled" || item.error().code == "closed") {
          break;
        }
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      received.push_back(item->index);
      // Credit releases when AdmittedFrame is destroyed after processing.
      consumed.fetch_add(1, std::memory_order_release);
    }
  });

  for (int index = 0; index < frames; ++index) {
    auto credit = credits->acquire();
    if (!credit) {
      consumer.request_stop();
      queue->close();
      return fail(20);
    }
    const auto in_flight = credits->in_flight();
    peak_in_flight.store(
        std::max(peak_in_flight.load(std::memory_order_relaxed), in_flight),
        std::memory_order_relaxed);
    AdmittedFrame frame{index, std::move(*credit)};
    if (!queue->push(std::move(frame))) {
      consumer.request_stop();
      queue->close();
      return fail(21);
    }
    produced.fetch_add(1, std::memory_order_release);
  }

  const auto drain_deadline = clock::now() + std::chrono::seconds(3);
  while (consumed.load(std::memory_order_acquire) < frames) {
    if (clock::now() > drain_deadline) {
      consumer.request_stop();
      queue->close();
      return fail(22);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  consumer.request_stop();
  queue->close();

  if (produced.load() != frames ||
      static_cast<int>(received.size()) != frames) {
    return fail(23);
  }
  for (int index = 0; index < frames; ++index) {
    if (received[static_cast<std::size_t>(index)] != index) {
      return fail(24);
    }
  }
  if (peak_in_flight.load() > k || credits->metrics().high_water > k ||
      queue->metrics().high_water > queue_cap ||
      credits->metrics().high_water != k) {
    return fail(25);
  }
  if (credits->metrics().blocked_acquire_count == 0U ||
      credits->metrics().blocked_wait_ns == 0U) {
    return fail(26);
  }
  if (kDefaultFrameCreditCapacity != 3U) {
    return fail(27);
  }
  return 0;
}

[[nodiscard]] int check_reorder_window() {
  auto buffer = ReorderBuffer<int>::create(3);
  if (!buffer) {
    return fail(28);
  }
  if (!buffer->insert(2, 20) || !buffer->insert(0, 0) ||
      !buffer->insert(1, 10)) {
    return fail(29);
  }
  if (buffer->insert(3, 30) ||
      buffer->insert(3, 30).error().code != "reorder_window_full") {
    return fail(30);
  }
  if (buffer->insert(0, 99) ||
      buffer->insert(0, 99).error().code != "duplicate_frame_index") {
    return fail(31);
  }
  auto a = buffer->try_pop_next();
  auto b = buffer->try_pop_next();
  auto c = buffer->try_pop_next();
  auto d = buffer->try_pop_next();
  if (!a || *a != 0 || !b || *b != 10 || !c || *c != 20 || d) {
    return fail(32);
  }
  if (buffer->insert(0, 1) ||
      buffer->insert(0, 1).error().code != "stale_frame_index") {
    return fail(33);
  }
  if (buffer->metrics().high_water < 3U) {
    return fail(34);
  }
  if (!buffer->insert(3, 30) || !buffer->insert(5, 50) ||
      !buffer->insert(4, 40)) {
    return fail(35);
  }
  for (int expected : {30, 40, 50}) {
    auto next = buffer->try_pop_next();
    if (!next || *next != expected) {
      return fail(36);
    }
  }
  if (buffer->try_pop_next()) {
    return fail(37);
  }
  return 0;
}

} // namespace

int main() {
  if (const int code = check_credit_capacity_and_release()) {
    return code;
  }
  if (const int code = check_credit_close_and_cancel()) {
    return code;
  }
  if (const int code = check_queue_capacity_one_and_stop()) {
    return code;
  }
  if (const int code = check_queue_close_drains()) {
    return code;
  }
  if (const int code = check_slow_sink_respects_k()) {
    return code;
  }
  if (const int code = check_reorder_window()) {
    return code;
  }
  return 0;
}
