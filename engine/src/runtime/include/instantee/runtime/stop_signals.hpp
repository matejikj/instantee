#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>
#include <memory>
#include <stop_token>

namespace instantee::runtime {

// Process entry-point service. Create on the main thread before starting any
// workers/native libraries; they must inherit its blocked SIGINT/SIGTERM mask.
// Exactly one bridge may exist. Destroy on the same thread, after joining all
// workers and retiring callbacks. Restores the original calling-thread mask.
// No asynchronous signal handler runs project code. Stop callbacks must be
// bounded and noexcept, and must not join the signal thread or call native
// cancellation.
class StopSignals {
public:
  StopSignals(const StopSignals &) = delete;
  StopSignals &operator=(const StopSignals &) = delete;
  ~StopSignals();

  [[nodiscard]] static std::expected<std::unique_ptr<StopSignals>, spike::Error>
  start(std::stop_source source);

private:
  struct Impl;
  explicit StopSignals(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::runtime
