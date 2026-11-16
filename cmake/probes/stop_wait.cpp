#include "contract.hpp"

#include <condition_variable>
#include <latch>
#include <mutex>
#include <stop_token>
#include <thread>

int main() {
  std::mutex mutex;
  std::condition_variable_any condition;
  std::latch waiting{1};
  bool predicate_satisfied = true;
  std::jthread worker{[&](std::stop_token stop) {
    std::unique_lock lock{mutex};
    waiting.count_down();
    predicate_satisfied = condition.wait(lock, stop, [] { return false; });
  }};
  waiting.wait();
  // Acquiring the lock proves the worker entered its wait before cancellation.
  {
    const std::lock_guard lock{mutex};
  }
  worker.request_stop();
  worker.join();
  if (predicate_satisfied) {
    return 1;
  }
  std::stop_source already_stopped;
  already_stopped.request_stop();
  std::unique_lock lock{mutex};
  if (condition.wait(lock, already_stopped.get_token(), [] { return false; })) {
    return 2;
  }
  return condition.wait(lock, already_stopped.get_token(), [] { return true; })
             ? 0
             : 3;
}
