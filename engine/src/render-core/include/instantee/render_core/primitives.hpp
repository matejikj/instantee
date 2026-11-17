#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstdint>
#include <expected>

namespace instantee::render_core {

// Binary coverage of a shape DrawOp at integer pixel (px, py). clear/unknown
// kinds return 0. Used by clip/mask compositing to AND visibility layers.
[[nodiscard]] float shape_coverage(const DrawOp &op, float px,
                                   float py) noexcept;

// Applies one DrawOp onto an RGBA8 image with straight src-over blending.
// clear replaces every pixel; shape ops blend coverage into existing pixels.
[[nodiscard]] std::expected<void, diagnostics::Error>
apply_draw_op(Rgba8Image &image, const DrawOp &op);

// Applies every op in order. Resizes the image when extents differ.
[[nodiscard]] std::expected<void, diagnostics::Error>
rasterize_pass(Rgba8Image &image, const PassDesc &pass);

} // namespace instantee::render_core
