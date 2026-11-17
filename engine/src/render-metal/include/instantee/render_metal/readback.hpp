#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_metal/frame.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::render_metal {
using diagnostics::Error;
using diagnostics::ExitCode;

// Move-only GPU readback submission. Owns the staging buffer and retains the
// command buffer plus source texture until GPU completion or destructor
// retirement. Destroying the lease never recycles resources while GPU work is
// still in flight; cancel only refuses pixel delivery.
class ReadbackLease {
public:
  enum class State : std::uint8_t {
    in_flight,
    ready,
    failed,
    cancelled,
    consumed
  };

  ReadbackLease(ReadbackLease &&) noexcept;
  ReadbackLease &operator=(ReadbackLease &&) noexcept;
  ~ReadbackLease();

  ReadbackLease(const ReadbackLease &) = delete;
  ReadbackLease &operator=(const ReadbackLease &) = delete;

  [[nodiscard]] State state() const noexcept;

  // Refuse later pixel delivery. Resources stay alive until GPU completion.
  void cancel() noexcept;

  // Waits for GPU completion when needed, then copies tightly packed RGBA8.
  // Fails after cancel or after a successful prior take.
  [[nodiscard]] std::expected<Rgba8Frame, Error> take_pixels();

private:
  friend class TestPatternRenderer;
  struct Impl;
  explicit ReadbackLease(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::render_metal
