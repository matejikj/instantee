#include <instantee/direction/layout_lint.hpp>

#include <instantee/direction/safe_area.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "layout_lint", message};
}

[[nodiscard]] bool finite_nonneg(double value) noexcept {
  return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool valid_rect(const LintRect &rect) noexcept {
  return std::isfinite(rect.x_px) && std::isfinite(rect.y_px) &&
         std::isfinite(rect.width_px) && std::isfinite(rect.height_px) &&
         rect.width_px >= 0.0 && rect.height_px >= 0.0;
}

[[nodiscard]] bool is_important(const LayoutLintElement &el,
                                double threshold) noexcept {
  return el.keep_visible || el.importance + 1e-12 >= threshold;
}

[[nodiscard]] LintRect to_lint_rect(const ContentSafeRect &rect) noexcept {
  return LintRect{.x_px = rect.x_px,
                  .y_px = rect.y_px,
                  .width_px = rect.width_px,
                  .height_px = rect.height_px};
}

void push_finding(std::vector<LintFinding> &out, std::string code,
                  LintSeverity severity, std::string path,
                  std::string element_id, std::string message) {
  out.push_back(LintFinding{.code = std::move(code),
                            .severity = severity,
                            .path = std::move(path),
                            .element_id = std::move(element_id),
                            .message = std::move(message)});
}

} // namespace

bool lint_rects_overlap(const LintRect &a, const LintRect &b) noexcept {
  return a.x_px < b.x_px + b.width_px && b.x_px < a.x_px + a.width_px &&
         a.y_px < b.y_px + b.height_px && b.y_px < a.y_px + a.height_px;
}

bool lint_rect_contains(const LintRect &outer, const LintRect &inner) noexcept {
  return inner.x_px + 1e-9 >= outer.x_px && inner.y_px + 1e-9 >= outer.y_px &&
         inner.x_px + inner.width_px <= outer.x_px + outer.width_px + 1e-9 &&
         inner.y_px + inner.height_px <= outer.y_px + outer.height_px + 1e-9;
}

std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_layout(std::span<const LayoutLintFrame> frames,
            const LayoutLintLimits &limits) {
  if (!(std::isfinite(limits.canvas_width_px) &&
        limits.canvas_width_px > 0.0) ||
      !(std::isfinite(limits.canvas_height_px) &&
        limits.canvas_height_px > 0.0) ||
      !(std::isfinite(limits.important_threshold) &&
        limits.important_threshold >= 0.0 &&
        limits.important_threshold <= 1.0)) {
    return std::unexpected(
        make_error("invalid_lint_limits",
                   "Layout lint limits must be finite and in range"));
  }

  auto content = content_safe_rect(limits.safe_area, limits.canvas_width_px,
                                   limits.canvas_height_px);
  if (!content) {
    return std::unexpected(content.error());
  }
  const LintRect content_rect = to_lint_rect(*content);
  const LintRect canvas{.x_px = 0,
                        .y_px = 0,
                        .width_px = limits.canvas_width_px,
                        .height_px = limits.canvas_height_px};

  std::vector<LintFinding> findings;
  for (const LayoutLintFrame &frame : frames) {
    for (const LayoutLintElement &el : frame.elements) {
      if (el.id.empty() || el.path.empty()) {
        return std::unexpected(
            make_error("invalid_lint_element",
                       "Layout lint elements require non-empty id and path"));
      }
      if (!valid_rect(el.bounds) || !finite_nonneg(el.importance) ||
          el.importance > 1.0) {
        return std::unexpected(
            make_error("invalid_lint_element",
                       "Layout lint element bounds/importance are invalid"));
      }
    }

    // Important content must remain on-canvas and inside the content safe rect.
    for (const LayoutLintElement &el : frame.elements) {
      if (!is_important(el, limits.important_threshold)) {
        continue;
      }
      if (!lint_rect_contains(canvas, el.bounds)) {
        push_finding(findings, "important_outside_canvas", LintSeverity::error,
                     el.path, el.id,
                     "Important content extends outside the canvas");
      }
      if (limits.safe_area.mode != SafeAreaMode::full_frame &&
          !lint_rect_contains(content_rect, el.bounds)) {
        push_finding(
            findings, "important_outside_safe_area", LintSeverity::error,
            el.path, el.id,
            "Important content overlaps a UI safe-area gutter or leaves the "
            "content rect");
      }
    }

    // Hard overlap: only between pairs where both elements are important.
    // This is a measurable collision signal, not an aesthetic judgment.
    for (std::size_t i = 0; i < frame.elements.size(); ++i) {
      const LayoutLintElement &a = frame.elements[i];
      if (!is_important(a, limits.important_threshold)) {
        continue;
      }
      for (std::size_t j = i + 1; j < frame.elements.size(); ++j) {
        const LayoutLintElement &b = frame.elements[j];
        if (!is_important(b, limits.important_threshold)) {
          continue;
        }
        if (!lint_rects_overlap(a.bounds, b.bounds)) {
          continue;
        }
        const LayoutLintElement *primary = &a;
        const LayoutLintElement *secondary = &b;
        if (b.path < a.path || (b.path == a.path && b.id < a.id)) {
          primary = &b;
          secondary = &a;
        }
        push_finding(findings, "hard_overlap", LintSeverity::error,
                     primary->path, primary->id,
                     "Important elements overlap: " + primary->id + " and " +
                         secondary->id);
      }
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
              if (a.element_id != b.element_id) {
                return a.element_id < b.element_id;
              }
              return a.message < b.message;
            });
  return findings;
}

} // namespace instantee::direction
