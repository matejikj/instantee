#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/readback.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::spike::metal {
using diagnostics::Error;
using diagnostics::ExitCode;

// Owns the offscreen target and compiled MSL pipeline. Each submission creates
// a move-only readback lease that owns staging until GPU completion.
// Borrows the Metal device/queue from Device for the renderer lifetime.
class TestPatternRenderer {
public:
  TestPatternRenderer(TestPatternRenderer &&) noexcept;
  TestPatternRenderer &operator=(TestPatternRenderer &&) noexcept;
  ~TestPatternRenderer();

  TestPatternRenderer(const TestPatternRenderer &) = delete;
  TestPatternRenderer &operator=(const TestPatternRenderer &) = delete;

  // Compiles the spike test-pattern library once and allocates the 1080x1920
  // RGBA8 target. device must outlive this renderer.
  [[nodiscard]] static std::expected<TestPatternRenderer, Error>
  create(Device &device);

  // Encodes one frame and returns a lease that keeps GPU resources alive until
  // completion, cancel retirement, or destruction. Supported frames: 0 and 1.
  [[nodiscard]] std::expected<ReadbackLease, Error>
  submit(std::uint32_t frame_index);

  // Convenience path: submit and take tightly packed RGBA8 pixels.
  [[nodiscard]] std::expected<Rgba8Frame, Error>
  render(std::uint32_t frame_index);

private:
  struct Impl;
  explicit TestPatternRenderer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::spike::metal
