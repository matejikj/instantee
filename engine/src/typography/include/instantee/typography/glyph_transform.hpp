#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/typography/kinetic_layout.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace instantee::typography {

// Pivot policy for glyph-local transforms. SAS keeps layout anchor distinct
// from per-glyph pivot; layout_anchor is the shared zoom/rotate fulcrum.
enum class GlyphPivotMode : std::uint8_t {
  glyph_origin = 1,
  glyph_center,
  layout_anchor,
};

// Uniform typographic gesture applied after shaping. Tracking adjusts pen
// positions; remaining fields build a pivot-preserving affine (core Mat4).
struct GlyphTransformSpec {
  double scale_x = 1.0;
  double scale_y = 1.0; // squeeze when scale_x != scale_y
  double rotation_z_rad = 0.0;
  double skew_x_rad = 0.0; // shear: x' = x + y * tan(skew_x)
  double skew_y_rad = 0.0; // shear: y' = y + x * tan(skew_y)
  // Extra advance inserted before glyph i (i * tracking), 26.6 pixels.
  std::int32_t tracking_26_6 = 0;
  GlyphPivotMode pivot = GlyphPivotMode::glyph_center;
  // Layout-space fulcrum in pixels when pivot == layout_anchor.
  double anchor_x_px = 0.0;
  double anchor_y_px = 0.0;
};

struct TransformedGlyph {
  std::uint32_t glyph_index = 0;
  // Maps glyph-local coordinates (origin at the untransformed pen) to layout.
  core::Mat4 matrix = core::Mat4::identity();
  double origin_x_px = 0.0;
  double origin_y_px = 0.0;
  double pivot_x_px = 0.0;
  double pivot_y_px = 0.0;
};

[[nodiscard]] constexpr std::string_view
glyph_pivot_mode_id(GlyphPivotMode mode) noexcept {
  switch (mode) {
  case GlyphPivotMode::glyph_origin:
    return "glyph_origin";
  case GlyphPivotMode::glyph_center:
    return "glyph_center";
  case GlyphPivotMode::layout_anchor:
    return "layout_anchor";
  }
  return "unknown";
}

// Convenience: uniform zoom about a fixed layout-space anchor (no jump).
[[nodiscard]] GlyphTransformSpec
make_anchor_preserving_zoom(double scale, double anchor_x_px,
                            double anchor_y_px) noexcept;

// Apply scale / tracking / rotation / skew / squeeze about the chosen pivot.
// Does not reshape; glyph_id / font_index stay with ShapedText.
[[nodiscard]] std::expected<std::vector<TransformedGlyph>, diagnostics::Error>
apply_glyph_transforms(const KineticLayout &layout, const ShapedText &shaped,
                       const GlyphTransformSpec &spec);

} // namespace instantee::typography
