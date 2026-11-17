#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/lint_types.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Bounding-box sample for one element at a critical frame. Geometry is
// injected from layout/composition; the linter does not invent aesthetics.
struct LayoutLintElement {
  std::string id;
  // Stable diagnostic path, e.g. "/shots/0/nodes/headline".
  std::string path;
  LintRect bounds{};
  double importance = 0.5;
  bool keep_visible = false;
};

// One sampled critical frame. Callers choose frames (e.g. cut points / holds);
// the linter only evaluates the supplied samples.
struct LayoutLintFrame {
  std::uint64_t frame = 0;
  std::vector<LayoutLintElement> elements;
};

struct LayoutLintLimits {
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
  // Importance >= threshold (or keep_visible) is treated as "important".
  double important_threshold = 0.7;
  SafeAreaPolicy safe_area{};
};

// Report hard overlaps between important elements and important content that
// leaves the canvas or the Style Bible content safe rect. Findings are sorted
// by (frame path key, code, element_id) for deterministic fixtures.
[[nodiscard]] std::expected<std::vector<LintFinding>, diagnostics::Error>
lint_layout(std::span<const LayoutLintFrame> frames,
            const LayoutLintLimits &limits = {});

[[nodiscard]] bool lint_rects_overlap(const LintRect &a,
                                      const LintRect &b) noexcept;

[[nodiscard]] bool lint_rect_contains(const LintRect &outer,
                                      const LintRect &inner) noexcept;

} // namespace instantee::direction
