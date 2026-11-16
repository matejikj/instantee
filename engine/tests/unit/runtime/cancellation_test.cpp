#include <csignal>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/stop_signals.hpp>
#include <pthread.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stop_token>
#include <thread>

namespace {
using instantee::runtime::BoundedQueue;

int check_abort_and_ownership() {
  auto queue = BoundedQueue<std::unique_ptr<int>>::create(1);
  auto first = std::make_unique<int>(7);
  auto second = std::make_unique<int>(9);
  if (!queue || !queue->push(std::move(first)) || first) {
    return 1;
  }
  std::stop_source stop;
  stop.request_stop();
  auto rejected = queue->push(std::move(second), stop.get_token());
  if (rejected || !second || *second != 9 || queue->size() != 1) {
    return 2;
  }
  // Stop never delivers already queued work to a cancelled consumer.
  if (queue->pop(stop.get_token()) || queue->size() != 1) {
    return 3;
  }
  queue->abort();
  queue->abort();
  queue->close();
  if (!queue->closed() || queue->size() != 0 || queue->pop() ||
      queue->push(std::move(second)) || !second) {
    return 4;
  }
  return 0;
}

struct Retirement {
  std::atomic_int *retired;
  BoundedQueue<std::unique_ptr<Retirement>> *queue;
  ~Retirement() {
    // Reentrant inspection proves retirement runs outside the queue mutex.
    if (queue->size() == 0) {
      ++*retired;
    }
  }
};

int check_abort_wakes_and_retires() {
  auto queue = BoundedQueue<std::unique_ptr<Retirement>>::create(1);
  if (!queue) {
    return 5;
  }
  std::atomic_int retired{0};
  auto item = std::unique_ptr<Retirement>(new Retirement{&retired, &*queue});
  if (!queue->push(std::move(item))) {
    return 6;
  }
  auto pending = std::unique_ptr<Retirement>(new Retirement{&retired, &*queue});
  bool cancelled = false;
  std::jthread producer([&] {
    auto result = queue->push(std::move(pending));
    cancelled = !result && result.error().code == "cancelled";
  });
  while (queue->metrics().blocked_push_count == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  queue->abort();
  producer.join();
  if (!cancelled || !pending || retired != 1) {
    return 7;
  }
  pending.reset();
  if (retired != 2) {
    return 8;
  }
  auto empty = BoundedQueue<int>::create(1);
  if (!empty) {
    return 9;
  }
  std::jthread consumer([&] {
    auto result = empty->pop();
    cancelled = !result && result.error().code == "cancelled";
  });
  while (empty->metrics().blocked_pop_count == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  empty->abort();
  consumer.join();
  return cancelled ? 0 : 10;
}
} // namespace

int main() {
  if (instantee::runtime::StopSignals::start(
          std::stop_source{std::nostopstate})) {
    return 13;
  }
  sigset_t original{};
  pthread_sigmask(SIG_SETMASK, nullptr, &original);
  for (int repeat = 0; repeat < 4; ++repeat) {
    std::stop_source stop;
    {
      auto signals = instantee::runtime::StopSignals::start(stop);
      if (!signals || instantee::runtime::StopSignals::start(stop)) {
        return 11;
      }
    }
    sigset_t restored{};
    pthread_sigmask(SIG_SETMASK, nullptr, &restored);
    if (stop.stop_requested() ||
        sigismember(&original, SIGINT) != sigismember(&restored, SIGINT) ||
        sigismember(&original, SIGTERM) != sigismember(&restored, SIGTERM)) {
      return 12;
    }
  }
  if (int result = check_abort_and_ownership()) {
    return result;
  }
  return check_abort_wakes_and_retires();
}
