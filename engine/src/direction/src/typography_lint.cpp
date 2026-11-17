#include <instantee/direction/typography_lint.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "typography_lint", message};
}

[[nodiscard]] bool finite_nonneg(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool valid_rect(const LintRect &rect) noexcept {
  return finite_nonneg(rect.x_px) && finite_nonneg(rect.y_px) &&
         std::isfinite(rect.width_px) && std::isfinite(rect.height_px) &&
         rect.width_px >= 0.0 && rect.height_px >= 0.0;
}

[[nodiscard]] bool contained(const LintRect &inner,
                             const LintRect &outer) noexcept {
  return inner.x_px + 1e-9 >= outer.x_px && inner.y_px + 1e-9 >= outer.y_px &&
         inner.x_px + inner.width_px <= outer.x_px + outer.width_px + 1e-9 &&
         inner.y_px + inner.height_px <= outer.y_px + outer.height_px + 1e-9;
}

[[nodiscard]] LintRect text_bounds(const TypographyLintElement &el) noexcept {
  if (el.text.has_value() && el.text->width_px > 0.0 &&
      el.text->height_px > 0.0) {
    return LintRect{.x_px = el.bounds.x_px,
                    .y_px = el.bounds.y_px,
                    .width_px = el.text->width_px,
                    .height_px = el.text->height_px};
  }
  return el.bounds;
}

void push_finding(std::vector<LintFinding> &out, std::string code,
                  LintSeverity severity, const TypographyLintElement &el,
                  std::string message) {
  out.push_back(LintFinding{.code = std::move(code),
                            .severity = severity,
                            .path = el.path,
                            .element_id = el.id,
                            .message = std::move(message)});
}

} // namespace

double relative_luminance(core::LinearRgba color) noexcept {
  // Inputs are already linear light (project working space).
  const double r = std::clamp(color.r, 0.0, 1.0);
  const double g = std::clamp(color.g, 0.0, 1.0);
  const double b = std::clamp(color.b, 0.0, 1.0);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double contrast_ratio(core::LinearRgba a, core::LinearRgba b) noexcept {
  const double l1 = relative_luminance(a);
  const double l2 = relative_luminance(b);
  const double lighter = std::max(l1, l2);
  const double darker = std::min(l1, l2);
  return (lighter + 0.05) / (darker + 0.05);
}

std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_typography(std::span<const TypographyLintElement> elements,
                const TypographyLintLimits &limits) {
  if (!(std::isfinite(limits.min_font_size_px) &&
        limits.min_font_size_px > 0.0) ||
      !(std::isfinite(limits.min_contrast_ratio) &&
        limits.min_contrast_ratio >= 1.0) ||
      !(std::isfinite(limits.large_text_min_contrast_ratio) &&
        limits.large_text_min_contrast_ratio >= 1.0) ||
      !(std::isfinite(limits.large_text_size_px) &&
        limits.large_text_size_px > 0.0) ||
      !(std::isfinite(limits.canvas_width_px) &&
        limits.canvas_width_px > 0.0) ||
      !(std::isfinite(limits.canvas_height_px) &&
        limits.canvas_height_px > 0.0)) {
    return std::unexpected(
        make_error("invalid_lint_limits",
                   "Typography lint limits must be finite and positive"));
  }

  std::vector<LintFinding> findings;
  const LintRect canvas{.x_px = 0,
                        .y_px = 0,
                        .width_px = limits.canvas_width_px,
                        .height_px = limits.canvas_height_px};

  for (const TypographyLintElement &el : elements) {
    if (el.id.empty() || el.path.empty()) {
      return std::unexpected(
          make_error("invalid_lint_element",
                     "Typography lint elements require non-empty id and path"));
    }
    if (!std::isfinite(el.font_size_px) || el.font_size_px <= 0.0) {
      return std::unexpected(make_error("invalid_font_size",
                                        "font_size_px must be finite and > 0"));
    }
    if (!valid_rect(el.bounds)) {
      return std::unexpected(
          make_error("invalid_bounds", "Element bounds must be finite"));
    }
    if (!core::finite_linear_rgba(el.foreground) ||
        !core::finite_linear_rgba(el.background)) {
      return std::unexpected(make_error(
          "invalid_color", "Foreground/background colors must be finite"));
    }
    if (el.mask.has_value() && !valid_rect(*el.mask)) {
      return std::unexpected(
          make_error("invalid_mask", "Mask rect must be finite"));
    }
    if (el.max_width_px.has_value() &&
        (!std::isfinite(*el.max_width_px) || *el.max_width_px <= 0.0)) {
      return std::unexpected(make_error("invalid_max_width",
                                        "max_width_px must be finite and > 0"));
    }
    if (el.text.has_value() &&
        (!std::isfinite(el.text->width_px) ||
         !std::isfinite(el.text->height_px) || el.text->width_px < 0.0 ||
         el.text->height_px < 0.0)) {
      return std::unexpected(make_error(
          "invalid_text_metrics", "Text metrics must be finite and >= 0"));
    }

    // 1) Minimum font size.
    if (el.font_size_px + 1e-9 < limits.min_font_size_px) {
      push_finding(findings, "min_font_size", LintSeverity::error, el,
                   "Font size is below the configured minimum");
    }

    // 2) Contrast under declared WCAG-style thresholds.
    const double required =
        (el.font_size_px + 1e-9 >= limits.large_text_size_px)
            ? limits.large_text_min_contrast_ratio
            : limits.min_contrast_ratio;
    const double ratio = contrast_ratio(el.foreground, el.background);
    if (ratio + 1e-9 < required) {
      push_finding(
          findings, "contrast_ratio", LintSeverity::error, el,
          "Foreground/background contrast is below the required ratio");
    }

    const LintRect glyph = text_bounds(el);

    // 3) Text clipped by an explicit mask/clip region.
    if (el.mask.has_value() && !contained(glyph, *el.mask)) {
      push_finding(findings, "text_mask_clip", LintSeverity::error, el,
                   "Text bounds extend outside the clip/mask region");
    }

    // 4) Overflow: measured width vs max_width / layout box, or outside canvas.
    if (el.max_width_px.has_value() && el.text.has_value() &&
        el.text->width_px > *el.max_width_px + 1e-9) {
      push_finding(findings, "text_overflow", LintSeverity::warning, el,
                   "Measured text width exceeds max_width_px");
    } else if (el.text.has_value() &&
               el.text->width_px > el.bounds.width_px + 1e-9) {
      push_finding(findings, "text_overflow", LintSeverity::warning, el,
                   "Measured text width exceeds the layout box width");
    }

    if (!contained(glyph, canvas)) {
      push_finding(findings, "text_overflow", LintSeverity::warning, el,
                   "Text bounds extend outside the canvas");
    }
  }

  std::sort(findings.begin(), findings.end(),
            [](const LintFinding &a, const LintFinding &b) {
              if (a.path != b.path) {
                return a.path < b.path;
              }
              if (a.code != b.code) {
                return a.code < b.code;
              }
              return a.element_id < b.element_id;
            });
  return findings;
}

} // namespace instantee::direction
