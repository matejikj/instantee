#pragma once

#include <cstdint>
#include <vector>

namespace instantee::render_core {

// GPU-neutral draw primitives. Backends map these to platform commands; the
// domain never names Metal/Apple types here. Keep the set small: later tasks
// extend kinds without introducing style-specific branches in this header.
enum class DrawOpKind : std::uint8_t {
  clear = 1,
  fill_rect = 2,
  fill_ellipse = 3,
  fill_rounded_rect = 4,
  stroke_line = 5,
};

// Axis-aligned rect in pass pixel space (origin top-left, y down).
struct Rect2f {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

struct DrawOp {
  DrawOpKind kind = DrawOpKind::clear;
  // fill_rect / fill_ellipse / fill_rounded_rect: axis-aligned bounds.
  // stroke_line: (x, y) is the start point; (x1, y1) is the end point.
  Rect2f rect{};
  float x1 = 0;
  float y1 = 0;
  Rgba8 color{};
  // Clamped to min(width,height)/2 during rasterization.
  float corner_radius = 0;
  // stroke_line half-extent is stroke_width/2 in pixel space.
  float stroke_width = 1;
};

// One render pass description. Resource handles stay opaque indices so
// render-core never owns GPU objects or FrameLease/admission credits.
struct PassDesc {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Generation-tagged target id assigned by a future resource registry.
  // Zero means "unbound" and submit must reject the pass.
  std::uint64_t color_target = 0;
  std::vector<DrawOp> ops;
};

[[nodiscard]] constexpr bool is_finite_positive_extent(float value) noexcept {
  return value > 0.0f;
}

[[nodiscard]] constexpr bool is_valid_rect(const Rect2f &rect) noexcept {
  return is_finite_positive_extent(rect.width) &&
         is_finite_positive_extent(rect.height);
}

} // namespace instantee::render_core
