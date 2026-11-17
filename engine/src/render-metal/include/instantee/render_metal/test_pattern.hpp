#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/pipeline_cache.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/readback.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::render_metal {
using diagnostics::Error;
using diagnostics::ExitCode;

// Owns the offscreen target and a retained pipeline state. Each submission
// creates a move-only readback lease that owns staging until GPU completion.
// Borrows the Metal device/queue from Device for the renderer lifetime.
class TestPatternRenderer {
public:
  TestPatternRenderer(TestPatternRenderer &&) noexcept;
  TestPatternRenderer &operator=(TestPatternRenderer &&) noexcept;
  ~TestPatternRenderer();

  TestPatternRenderer(const TestPatternRenderer &) = delete;
  TestPatternRenderer &operator=(const TestPatternRenderer &) = delete;

  // Loads the offline-pinned test-pattern metallib, resolves the pipeline via
  // `pipeline_cache` (or a local one-shot cache when null), and allocates the
  // 1080x1920 RGBA8 target. device must outlive this renderer. The cache, when
  // provided, must remain valid for subsequent shared lookups; the renderer
  // retains its own pipeline reference and does not borrow cache storage.
  [[nodiscard]] static std::expected<TestPatternRenderer, Error>
  create(Device &device, render_core::PipelineCache *pipeline_cache = nullptr);

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

} // namespace instantee::render_metal
