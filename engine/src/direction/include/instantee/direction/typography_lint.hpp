#pragma once

#include <instantee/core_model/color.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/lint_types.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// One text-bearing layout sample for contrast/typography checks. Metrics are
// injected (typography adapter or fixture); the linter does not link HarfBuzz.
struct TypographyLintElement {
  std::string id;
  // Stable diagnostic path, e.g. "/shots/0/nodes/headline".
  std::string path;
  double font_size_px = 0;
  LintRect bounds{};
  core::LinearRgba foreground{};
  core::LinearRgba background{};
  std::optional<TextMetrics> text{};
  // Optional clip/mask; text bounds must stay inside when present.
  std::optional<LintRect> mask{};
  // Optional authoring max width; overflow when measured width exceeds it.
  std::optional<double> max_width_px{};
};

struct TypographyLintLimits {
  double min_font_size_px = 24.0;
  // WCAG 2.x contrast ratio for normal text (AA). Large text may lower this
  // via large_text_min_contrast_ratio when font_size_px >= large_text_size_px.
  double min_contrast_ratio = 4.5;
  double large_text_min_contrast_ratio = 3.0;
  double large_text_size_px = 36.0;
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
};

// Run contrast/typography policy checks. Findings are sorted by
// (path, code, element_id) for deterministic fixtures.
[[nodiscard]] std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_typography(std::span<const TypographyLintElement> elements,
                const TypographyLintLimits &limits = {});

// Relative luminance of a linear-light RGB color (WCAG 2.x coefficients).
[[nodiscard]] double relative_luminance(core::LinearRgba color) noexcept;

// Contrast ratio of two linear-light colors, always >= 1.
[[nodiscard]] double contrast_ratio(core::LinearRgba a,
                                    core::LinearRgba b) noexcept;

} // namespace instantee::direction
