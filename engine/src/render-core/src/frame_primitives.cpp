#include <instantee/render_core/frame_primitives.hpp>

#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "frame_primitives", message};
}

struct Vec2 {
  float x = 0;
  float y = 0;
};

[[nodiscard]] Vec2 transform_xy(const std::array<float, 16> &m, float x,
                                float y) noexcept {
  // Column-major: translation in m[12], m[13].
  const float xw = m[0] * x + m[4] * y + m[12];
  const float yw = m[1] * x + m[5] * y + m[13];
  return Vec2{xw, yw};
}

[[nodiscard]] Rect2f aabb_of_rect(const std::array<float, 16> &m,
                                  const Rect2f &local) noexcept {
  const Vec2 p0 = transform_xy(m, local.x, local.y);
  const Vec2 p1 = transform_xy(m, local.x + local.width, local.y);
  const Vec2 p2 = transform_xy(m, local.x, local.y + local.height);
  const Vec2 p3 =
      transform_xy(m, local.x + local.width, local.y + local.height);
  const float min_x = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
  const float min_y = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
  const float max_x = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
  const float max_y = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
  return Rect2f{min_x, min_y, max_x - min_x, max_y - min_y};
}

} // namespace

std::expected<PassDesc, diagnostics::Error>
lower_primitives_to_pass(std::span<const WorldPrimitive> primitives,
                         const LowerPrimitivesConfig &config) {
  if (config.width_px == 0 || config.height_px == 0) {
    return std::unexpected(make_invalid(
        "invalid_pass_extent", "lowered pass width/height must be > 0"));
  }
  if (config.color_target == 0) {
    return std::unexpected(make_invalid(
        "unbound_color_target", "lowered pass requires a color_target"));
  }

  PassDesc pass;
  pass.width_px = config.width_px;
  pass.height_px = config.height_px;
  pass.color_target = config.color_target;
  pass.ops.reserve(primitives.size() + 1U);
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = config.clear_color});

  for (const WorldPrimitive &prim : primitives) {
    DrawOp op;
    op.kind = prim.kind;
    op.color = prim.color;
    op.corner_radius = prim.corner_radius;
    op.stroke_width = prim.stroke_width;
    switch (prim.kind) {
    case DrawOpKind::clear:
      return std::unexpected(make_invalid(
          "invalid_primitive_kind",
          "WorldPrimitive cannot use clear; clear is inserted by lowering"));
    case DrawOpKind::fill_rect:
    case DrawOpKind::fill_ellipse:
    case DrawOpKind::fill_rounded_rect: {
      if (!is_valid_rect(prim.local_bounds)) {
        return std::unexpected(make_invalid(
            "invalid_local_bounds",
            "fill primitive requires positive local width/height"));
      }
      op.rect = aabb_of_rect(prim.world_col_major, prim.local_bounds);
      // Uniform scale approximation for corner radius.
      const float sx =
          std::hypot(prim.world_col_major[0], prim.world_col_major[1]);
      const float sy =
          std::hypot(prim.world_col_major[4], prim.world_col_major[5]);
      op.corner_radius = prim.corner_radius * std::min(sx, sy);
      break;
    }
    case DrawOpKind::stroke_line: {
      const Vec2 a = transform_xy(prim.world_col_major, prim.local_bounds.x,
                                  prim.local_bounds.y);
      const Vec2 b =
          transform_xy(prim.world_col_major, prim.local_x1, prim.local_y1);
      op.rect.x = a.x;
      op.rect.y = a.y;
      op.x1 = b.x;
      op.y1 = b.y;
      const float sx =
          std::hypot(prim.world_col_major[0], prim.world_col_major[1]);
      const float sy =
          std::hypot(prim.world_col_major[4], prim.world_col_major[5]);
      op.stroke_width = prim.stroke_width * (0.5f * (sx + sy));
      break;
    }
    default:
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown primitive DrawOpKind"));
    }
    pass.ops.push_back(op);
  }

  auto valid = validate_pass(pass);
  if (!valid) {
    return std::unexpected(valid.error());
  }
  return pass;
}

} // namespace instantee::render_core
