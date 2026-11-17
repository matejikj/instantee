#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/clip_compositor.hpp>
#include <instantee/render_core/pipeline_cache.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>

#include <expected>
#include <memory>

namespace instantee::render_metal {
using diagnostics::Error;

// GPU executor for ClipScript: axis-aligned layers use Metal scissor; shape
// masks use a stencil increment/decrement nest. Public surface stays C++-only.
class ClipMaskRenderer {
public:
  ClipMaskRenderer(ClipMaskRenderer &&) noexcept;
  ClipMaskRenderer &operator=(ClipMaskRenderer &&) noexcept;
  ~ClipMaskRenderer();

  ClipMaskRenderer(const ClipMaskRenderer &) = delete;
  ClipMaskRenderer &operator=(const ClipMaskRenderer &) = delete;

  [[nodiscard]] static std::expected<ClipMaskRenderer, Error>
  create(Device &device, render_core::PipelineCache *pipeline_cache = nullptr);

  // Renders `script` into a fresh RGBA8 target. Must match the CPU compositor
  // for binary coverage shapes used in goldens.
  [[nodiscard]] std::expected<Rgba8Frame, Error>
  render(const render_core::ClipScript &script);

private:
  struct Impl;
  explicit ClipMaskRenderer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::render_metal
