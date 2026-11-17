#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstdint>
#include <expected>
#include <vector>

namespace instantee::render_core {

// Per-instance attributes packed into one GPU/CPU instance buffer. Geometry
// identity (kind) and style vary per instance; pipeline/blend/texture stay on
// the batch. Layout matches the Metal primitives instance buffer.
struct InstanceAttrib {
  Rect2f rect{};
  float x1 = 0;
  float y1 = 0;
  Rgba8 color{};
  float corner_radius = 0;
  float stroke_width = 1;
  DrawOpKind kind = DrawOpKind::fill_rect;
};

// One draw call over an instance buffer. Consecutive ops share texture_binding
// and src-over blend; transparent order is preserved by keeping source order
// inside `instances` and never reordering across batches.
struct DrawBatch {
  std::uint64_t texture_binding = 0;
  std::vector<InstanceAttrib> instances{};
};

struct BatchStats {
  std::uint32_t source_ops = 0; // non-clear ops in the source pass
  std::uint32_t draw_calls = 0; // batches emitted (= GPU draw calls)
  std::uint32_t clear_ops = 0;
  std::uint32_t batch_splits = 0; // texture/order barriers that flushed
};

struct BatchedPass {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::uint64_t color_target = 0;
  Rgba8 clear_color{0, 0, 0, 255};
  bool has_clear = false;
  std::vector<DrawBatch> batches{};
  BatchStats stats{};
};

// Coalesce consecutive compatible shape ops into instance batches. Clears may
// appear only before the first shape (last prefix clear wins). A clear after
// shapes must be a separate PassDesc — transparent order stays source order.
[[nodiscard]] std::expected<BatchedPass, diagnostics::Error>
batch_pass(const PassDesc &pass);

// Expand batches back to a PassDesc with one DrawOp per instance (plus clear).
// Useful for pixel goldens against the naive rasterizer.
[[nodiscard]] PassDesc expand_batched_pass(const BatchedPass &batched);

// Rasterize via expand + rasterize_pass; identical pixels to the source pass
// when batching only coalesces without reordering.
[[nodiscard]] std::expected<void, diagnostics::Error>
rasterize_batched_pass(Rgba8Image &image, const BatchedPass &batched);

} // namespace instantee::render_core
