#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace instantee::render_core {

// Local-space primitive bound to a node world matrix. Matrices are column-major
// 4×4 matching core::Mat4 storage. Lowering maps them into pass pixel space.
struct WorldPrimitive {
  DrawOpKind kind = DrawOpKind::fill_rect;
  Rect2f local_bounds{};
  float local_x1 = 0;
  float local_y1 = 0;
  float corner_radius = 0;
  float stroke_width = 1;
  Rgba8 color{};
  std::array<float, 16> world_col_major{
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
  };
};

struct LowerPrimitivesConfig {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::uint64_t color_target = 1;
  Rgba8 clear_color{0, 0, 0, 255};
};

// Builds a single pass: clear, then each visible primitive in caller order.
// Axis-aligned fills use the world-transformed AABB; lines transform endpoints.
[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
lower_primitives_to_pass(std::span<const WorldPrimitive> primitives,
                         const LowerPrimitivesConfig &config);

} // namespace instantee::render_core
