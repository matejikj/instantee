#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/pipeline_metrics.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>

namespace instantee::runtime {

// Default experimental global in-flight frame budget (K). K=2 is also valid for
// profiling; encoder-owned pixel buffers use a separate limit E.
inline constexpr std::uint32_t kDefaultFrameCreditCapacity = 3;

namespace detail {
struct FrameCreditPoolState;
}

// Move-only admission token. Destruction returns the credit to the pool. Does
// not wait on GPU/encoder and does not publish output.
class FrameCredit {
public:
  FrameCredit() noexcept = default;
  FrameCredit(FrameCredit &&other) noexcept;
  FrameCredit &operator=(FrameCredit &&other) noexcept;
  ~FrameCredit();

  FrameCredit(const FrameCredit &) = delete;
  FrameCredit &operator=(const FrameCredit &) = delete;

  [[nodiscard]] bool empty() const noexcept { return state_ == nullptr; }
  [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

  // Releases the credit early. Safe to call repeatedly.
  void release() noexcept;

private:
  friend class FrameCreditPool;
  explicit FrameCredit(
      std::shared_ptr<detail::FrameCreditPoolState> state) noexcept;

  std::shared_ptr<detail::FrameCreditPoolState> state_{};
};

// Global application-frame admission gate. Acquire before allocating/evaluating
// a frame; outstanding credits never exceed capacity.
class FrameCreditPool {
public:
  FrameCreditPool(FrameCreditPool &&) noexcept;
  FrameCreditPool &operator=(FrameCreditPool &&) noexcept;
  ~FrameCreditPool();

  FrameCreditPool(const FrameCreditPool &) = delete;
  FrameCreditPool &operator=(const FrameCreditPool &) = delete;

  [[nodiscard]] static std::expected<FrameCreditPool, diagnostics::Error>
  create(std::uint32_t capacity = kDefaultFrameCreditCapacity);

  [[nodiscard]] std::uint32_t capacity() const noexcept;
  [[nodiscard]] std::uint32_t in_flight() const noexcept;
  [[nodiscard]] bool closed() const noexcept;
  [[nodiscard]] PipelineMetrics metrics() const noexcept;

  // Wakes blocked acquires with a closed result. Does not release held credits.
  void close() noexcept;

  [[nodiscard]] std::expected<FrameCredit, diagnostics::Error>
  acquire(std::stop_token stop = {},
          std::optional<std::chrono::steady_clock::time_point> deadline =
              std::nullopt);

private:
  explicit FrameCreditPool(
      std::shared_ptr<detail::FrameCreditPoolState> state) noexcept;

  std::shared_ptr<detail::FrameCreditPoolState> state_{};
};

} // namespace instantee::runtime
