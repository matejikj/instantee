#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/pipeline_cache.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>

#include <cstdint>
#include <expected>
#include <memory>

namespace instantee::render_metal {
using diagnostics::Error;
using diagnostics::ExitCode;

// GPU rasterizer for the render-core primitive DrawOp set. Builds an offscreen
// RGBA8 target, encodes clear + shape draws with src-over blending, then
// returns tightly packed pixels. Optional PipelineCache reuses the SDF PSO.
class PrimitiveRenderer {
public:
  PrimitiveRenderer(PrimitiveRenderer &&) noexcept;
  PrimitiveRenderer &operator=(PrimitiveRenderer &&) noexcept;
  ~PrimitiveRenderer();

  PrimitiveRenderer(const PrimitiveRenderer &) = delete;
  PrimitiveRenderer &operator=(const PrimitiveRenderer &) = delete;

  [[nodiscard]] static std::expected<PrimitiveRenderer, Error>
  create(Device &device, render_core::PipelineCache *pipeline_cache = nullptr);

  // Renders `pass` into a fresh target matching pass extents. Rejects passes
  // that fail render-core validation.
  [[nodiscard]] std::expected<Rgba8Frame, Error>
  render(const render_core::PassDesc &pass);

private:
  struct Impl;
  explicit PrimitiveRenderer(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::render_metal
