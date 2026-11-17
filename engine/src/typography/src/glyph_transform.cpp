#include <instantee/typography/glyph_transform.hpp>

#include <cmath>
#include <utility>

namespace instantee::typography {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "glyph_transform", message};
}

[[nodiscard]] bool finite_number(double v) noexcept { return std::isfinite(v); }

[[nodiscard]] core::Mat4 shear_x(double tan_k) noexcept {
  core::Mat4 out = core::Mat4::identity();
  out(0, 1) = tan_k;
  return out;
}

[[nodiscard]] core::Mat4 shear_y(double tan_k) noexcept {
  core::Mat4 out = core::Mat4::identity();
  out(1, 0) = tan_k;
  return out;
}

[[nodiscard]] std::expected<GlyphTransformSpec, diagnostics::Error>
validate(const GlyphTransformSpec &spec) {
  if (!finite_number(spec.scale_x) || !finite_number(spec.scale_y) ||
      !finite_number(spec.rotation_z_rad) || !finite_number(spec.skew_x_rad) ||
      !finite_number(spec.skew_y_rad) || !finite_number(spec.anchor_x_px) ||
      !finite_number(spec.anchor_y_px)) {
    return std::unexpected(make_invalid(
        "non_finite_transform", "Glyph transform parameters must be finite"));
  }
  if (spec.scale_x == 0.0 || spec.scale_y == 0.0) {
    return std::unexpected(make_invalid(
        "zero_scale", "Glyph transform scale components must be non-zero"));
  }
  return spec;
}

} // namespace

GlyphTransformSpec make_anchor_preserving_zoom(double scale, double anchor_x_px,
                                               double anchor_y_px) noexcept {
  GlyphTransformSpec spec;
  spec.scale_x = scale;
  spec.scale_y = scale;
  spec.pivot = GlyphPivotMode::layout_anchor;
  spec.anchor_x_px = anchor_x_px;
  spec.anchor_y_px = anchor_y_px;
  return spec;
}

std::expected<std::vector<TransformedGlyph>, diagnostics::Error>
apply_glyph_transforms(const KineticLayout &layout, const ShapedText &shaped,
                       const GlyphTransformSpec &spec) {
  if (auto valid = validate(spec); !valid) {
    return std::unexpected(valid.error());
  }
  if (layout.glyph_origin_x_26_6.size() != shaped.glyphs.size() ||
      layout.glyph_origin_y_26_6.size() != shaped.glyphs.size() ||
      layout.glyphs.size() != shaped.glyphs.size()) {
    return std::unexpected(make_invalid(
        "layout_shaped_mismatch",
        "Kinetic layout glyph origins must match shaped glyph count"));
  }

  const double track_px = static_cast<double>(spec.tracking_26_6) / 64.0;
  const double kx = std::tan(spec.skew_x_rad);
  const double ky = std::tan(spec.skew_y_rad);
  if (!finite_number(kx) || !finite_number(ky)) {
    return std::unexpected(make_invalid(
        "skew_overflow", "Glyph skew angle produces a non-finite shear"));
  }

  const core::Mat4 skew = core::mul(shear_y(ky), shear_x(kx));
  const core::Mat4 rot = core::rotation_z(spec.rotation_z_rad);
  const core::Mat4 scale =
      core::scaling(core::Vec3{spec.scale_x, spec.scale_y, 1.0});
  // Skew, then rotate, then scale about the pivot (column-vector: rightmost
  // first). Tracking only shifts the untransformed pen.
  const core::Mat4 linear = core::mul(skew, core::mul(rot, scale));

  std::vector<TransformedGlyph> out;
  out.resize(shaped.glyphs.size());
  for (std::uint32_t i = 0; i < shaped.glyphs.size(); ++i) {
    const double base_x =
        static_cast<double>(layout.glyph_origin_x_26_6[i]) / 64.0 +
        track_px * static_cast<double>(i);
    const double base_y =
        static_cast<double>(layout.glyph_origin_y_26_6[i]) / 64.0;
    const double advance =
        static_cast<double>(shaped.glyphs[i].x_advance_26_6) / 64.0;

    double pivot_x = base_x;
    double pivot_y = base_y;
    switch (spec.pivot) {
    case GlyphPivotMode::glyph_origin:
      break;
    case GlyphPivotMode::glyph_center:
      pivot_x = base_x + advance * 0.5;
      pivot_y = base_y;
      break;
    case GlyphPivotMode::layout_anchor:
      pivot_x = spec.anchor_x_px;
      pivot_y = spec.anchor_y_px;
      break;
    }

    const core::Mat4 to_pivot =
        core::translation(core::Vec3{pivot_x, pivot_y, 0});
    const core::Mat4 from_pivot =
        core::translation(core::Vec3{-pivot_x, -pivot_y, 0});
    const core::Mat4 place = core::translation(core::Vec3{base_x, base_y, 0});
    // layout = T(pivot) * linear * T(-pivot) * T(base) * p_local
    const core::Mat4 matrix =
        core::mul(to_pivot, core::mul(linear, core::mul(from_pivot, place)));

    TransformedGlyph sample;
    sample.glyph_index = i;
    sample.matrix = matrix;
    sample.pivot_x_px = pivot_x;
    sample.pivot_y_px = pivot_y;
    const core::Vec3 origin =
        core::transform_point(matrix, core::Vec3{0, 0, 0});
    sample.origin_x_px = origin.x;
    sample.origin_y_px = origin.y;
    out[i] = sample;
  }
  return out;
}

} // namespace instantee::typography
