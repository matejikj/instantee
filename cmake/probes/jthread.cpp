#include "contract.hpp"

#include <atomic>
#include <latch>
#include <stop_token>
#include <thread>

#if !defined(__cpp_lib_jthread) || __cpp_lib_jthread < 201911L
#error "std::jthread is required"
#endif

int main() {
  std::latch started{1};
  std::atomic<bool> observed_stop{false};
  {
    std::jthread worker{[&](std::stop_token stop) noexcept {
      std::stop_callback callback{stop, [&]() noexcept {
                                    observed_stop.store(true);
                                    observed_stop.notify_one();
                                  }};
      started.count_down();
      observed_stop.wait(false);
    }};
    started.wait();
    // Destruction must request stop and join before dependencies are destroyed.
  }
  return observed_stop.load() ? 0 : 1;
}
