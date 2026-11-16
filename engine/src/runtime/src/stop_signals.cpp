#include <instantee/runtime/stop_signals.hpp>

#include <atomic>
#include <cassert>
#include <csignal>
#include <pthread.h>
#include <thread>
#include <utility>

namespace instantee::runtime {
namespace {
std::atomic_bool active{false};
constexpr spike::Error unavailable{spike::ExitCode::export_failure,
                                   "signal_bridge_unavailable", "signals",
                                   "Cannot start the process signal bridge"};
} // namespace

struct StopSignals::Impl {
  std::stop_source source;
  sigset_t signals{};
  sigset_t original{};
  pthread_t owner = pthread_self();
  std::atomic_bool closing{false};
  bool masked = false;
  std::thread listener;

  explicit Impl(std::stop_source source) : source(std::move(source)) {}

  ~Impl() {
    assert(pthread_equal(owner, pthread_self()));
    closing.store(true, std::memory_order_release);
    if (listener.joinable()) {
      // A thread-directed blocked signal wakes sigwait without changing any
      // process-wide disposition or interrupting unrelated native work.
      static_cast<void>(pthread_kill(listener.native_handle(), SIGTERM));
      listener.join();
    }
    if (masked) {
      static_cast<void>(pthread_sigmask(SIG_SETMASK, &original, nullptr));
    }
    active.store(false, std::memory_order_release);
  }

  void wait() noexcept {
    for (;;) {
      int signal = 0;
      const int result = sigwait(&signals, &signal);
      if (closing.load(std::memory_order_acquire)) {
        return;
      }
      // Fail closed on an unexpected sigwait error, without retry spinning.
      source.request_stop();
      if (result != 0) {
        return;
      }
    }
  }
};

StopSignals::StopSignals(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
StopSignals::~StopSignals() = default;

std::expected<std::unique_ptr<StopSignals>, spike::Error>
StopSignals::start(std::stop_source source) {
  if (!source.stop_possible()) {
    return std::unexpected(unavailable);
  }
  bool expected = false;
  if (!active.compare_exchange_strong(expected, true)) {
    return std::unexpected(unavailable);
  }
  std::unique_ptr<Impl> impl;
  try {
    impl = std::make_unique<Impl>(std::move(source));
    sigemptyset(&impl->signals);
    sigaddset(&impl->signals, SIGINT);
    sigaddset(&impl->signals, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &impl->signals, &impl->original) != 0) {
      return std::unexpected(unavailable);
    }
    impl->masked = true;
    impl->listener = std::thread([state = impl.get()] { state->wait(); });
    return std::unique_ptr<StopSignals>(new StopSignals(std::move(impl)));
  } catch (...) {
    if (!impl) {
      active.store(false, std::memory_order_release);
    }
    return std::unexpected(unavailable);
  }
}
} // namespace instantee::runtime
