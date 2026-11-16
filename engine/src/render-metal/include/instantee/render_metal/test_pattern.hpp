#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::spike::metal {

// Owns the offscreen target, compiled MSL pipeline and staging buffer.
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

  // Renders one time-varying frame and synchronously reads it back to CPU.
  // Supported frame indices for the spike oracle are 0 and 1.
  [[nodiscard]] std::expected<Rgba8Frame, Error>
  render(std::uint32_t frame_index);

private:
  struct Impl;
  explicit TestPatternRenderer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::spike::metal
