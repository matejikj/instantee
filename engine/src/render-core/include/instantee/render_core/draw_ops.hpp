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
  Rect2f rect{};
  Rgba8 color{};
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
