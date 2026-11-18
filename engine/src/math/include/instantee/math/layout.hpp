#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/math/ast.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace instantee::math {

// Ink metrics relative to the text baseline (TeX height/depth convention).
struct MathAtomMetrics {
  double width_px = 0;
  double height_px = 0; // above baseline
  double depth_px = 0;  // below baseline
};

// Port for glyph measurement. Production math never links FreeType/HarfBuzz;
// callers inject shaping or synthetic metrics.
class MathMetricsProvider {
public:
  virtual ~MathMetricsProvider() = default;

  [[nodiscard]] virtual std::expected<MathAtomMetrics, diagnostics::Error>
  measure(std::string_view text, float size_px) const = 0;
};

struct MathLayoutStyle {
  float size_px = 32.0f;
  float script_scale = 0.7f;
  float script_space_factor = 0.05f;
  float frac_pad_factor = 0.1f;
  float frac_gap_factor = 0.15f;
  float frac_rule_factor = 0.05f;
  float axis_factor = 0.25f;
  float sqrt_gap_factor = 0.08f;
  float sqrt_rule_factor = 0.05f;
  float sqrt_surd_width_factor = 0.55f;
  float index_scale = 0.6f;
  float index_hspace_factor = 0.05f;
  float index_raise_factor = 0.55f;
};

// Laid-out box tree. Coordinates: child.x_px from parent left; child.shift_px
// raises the child baseline relative to the parent baseline.
struct MathLayoutBox {
  double width_px = 0;
  double height_px = 0;
  double depth_px = 0;
  double x_px = 0;
  double shift_px = 0;
  MathNodeKind source_kind = MathNodeKind::row;
  std::string tag{};
  std::vector<MathLayoutBox> children{};
};

// Deterministic unit metrics for goldens: each UTF-8 code point is
// 0.5·size wide, 0.8·size tall, 0.2·size deep. Independent of fonts.
class UnitMathMetrics final : public MathMetricsProvider {
public:
  [[nodiscard]] std::expected<MathAtomMetrics, diagnostics::Error>
  measure(std::string_view text, float size_px) const override;
};

// Map AST leaves to measurable text (symbols → short ASCII stand-ins so unit
// metrics stay stable without requiring Unicode coverage).
[[nodiscard]] std::string math_leaf_text(const MathNode &node);

[[nodiscard]] std::expected<MathLayoutBox, diagnostics::Error>
layout_math(const MathNode &root, const MathMetricsProvider &metrics,
            const MathLayoutStyle &style = {});

// Deterministic structural dump for golden fixtures (3 decimal places).
[[nodiscard]] std::string format_math_layout_golden(const MathLayoutBox &box);

} // namespace instantee::math
