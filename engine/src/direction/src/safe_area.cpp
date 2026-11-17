#include <instantee/direction/safe_area.hpp>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "safe_area", message};
}

[[nodiscard]] bool finite_positive(double value) noexcept {
  return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool element_protected(const LayoutElement &el, SafeAreaMode mode,
                                     double important_threshold) noexcept {
  if (mode == SafeAreaMode::reels_clear) {
    return true;
  }
  if (mode == SafeAreaMode::full_frame) {
    return false;
  }
  // reels_overlay: protect keep_visible and high-importance content only.
  return el.keep_visible || el.importance + 1e-12 >= important_threshold;
}

} // namespace

std::expected<ContentSafeRect, diagnostics::Error>
content_safe_rect(const SafeAreaPolicy &policy, double canvas_width_px,
                  double canvas_height_px) {
  if (!finite_positive(canvas_width_px) || !finite_positive(canvas_height_px)) {
    return std::unexpected(make_error(
        "invalid_canvas", "Canvas dimensions must be finite and > 0"));
  }
  if (!std::isfinite(policy.top_px) || !std::isfinite(policy.bottom_px) ||
      !std::isfinite(policy.left_px) || !std::isfinite(policy.right_px) ||
      policy.top_px < 0.0 || policy.bottom_px < 0.0 || policy.left_px < 0.0 ||
      policy.right_px < 0.0) {
    return std::unexpected(make_error("invalid_safe_area_insets",
                                      "Safe-area insets must be >= 0"));
  }

  double top = policy.top_px;
  double bottom = policy.bottom_px;
  double left = policy.left_px;
  double right = policy.right_px;
  if (policy.mode == SafeAreaMode::full_frame) {
    top = bottom = left = right = 0.0;
  }

  if (top + bottom >= canvas_height_px || left + right >= canvas_width_px) {
    return std::unexpected(
        make_error("unusable_safe_area",
                   "Safe-area insets leave no usable content rectangle"));
  }

  return ContentSafeRect{
      .x_px = left,
      .y_px = top,
      .width_px = canvas_width_px - left - right,
      .height_px = canvas_height_px - top - bottom,
  };
}

std::expected<UiSafeRegions, diagnostics::Error>
ui_safe_regions(const SafeAreaPolicy &policy, double canvas_width_px,
                double canvas_height_px) {
  auto content = content_safe_rect(policy, canvas_width_px, canvas_height_px);
  if (!content) {
    return std::unexpected(content.error());
  }
  UiSafeRegions regions;
  if (policy.mode == SafeAreaMode::full_frame) {
    return regions;
  }
  regions.has_overlay_bands = true;
  regions.top = ContentSafeRect{.x_px = 0,
                                .y_px = 0,
                                .width_px = canvas_width_px,
                                .height_px = content->y_px};
  regions.bottom = ContentSafeRect{
      .x_px = 0,
      .y_px = content->y_px + content->height_px,
      .width_px = canvas_width_px,
      .height_px = canvas_height_px - (content->y_px + content->height_px)};
  regions.left = ContentSafeRect{.x_px = 0,
                                 .y_px = content->y_px,
                                 .width_px = content->x_px,
                                 .height_px = content->height_px};
  regions.right = ContentSafeRect{
      .x_px = content->x_px + content->width_px,
      .y_px = content->y_px,
      .width_px = canvas_width_px - (content->x_px + content->width_px),
      .height_px = content->height_px};
  return regions;
}

std::expected<CompositionSolution, diagnostics::Error>
enforce_safe_area(std::span<const LayoutElement> elements,
                  const SafeAreaPolicy &policy, double canvas_width_px,
                  double canvas_height_px, SafeAreaLayoutOptions options) {
  auto content = content_safe_rect(policy, canvas_width_px, canvas_height_px);
  if (!content) {
    return std::unexpected(content.error());
  }

  if (policy.mode == SafeAreaMode::full_frame) {
    // No gutter: still run a canvas keep-visible pass for flagged elements.
    std::vector<CompositionConstraint> constraints;
    for (const LayoutElement &el : elements) {
      if (el.keep_visible) {
        constraints.push_back({.kind = CompositionConstraintKind::keep_visible,
                               .severity = ConstraintSeverity::hard,
                               .element_id = el.id});
      }
    }
    return solve_composition(elements, constraints, canvas_width_px,
                             canvas_height_px);
  }

  constexpr std::string_view kContentFrameId = "__safe_content__";
  std::vector<LayoutElement> working;
  working.reserve(elements.size() + 1);
  working.push_back(LayoutElement{
      .id = std::string(kContentFrameId),
      .x_px = content->x_px,
      .y_px = content->y_px,
      .width_px = content->width_px,
      .height_px = content->height_px,
      .importance = 1.0,
  });
  for (const LayoutElement &el : elements) {
    working.push_back(el);
  }

  std::vector<CompositionConstraint> constraints;
  for (const LayoutElement &el : elements) {
    if (!element_protected(el, policy.mode, options.important_threshold)) {
      continue;
    }
    if (el.width_px > content->width_px || el.height_px > content->height_px) {
      return std::unexpected(
          make_error("unsatisfiable_safe_area",
                     "Protected element is larger than the content safe rect"));
    }
    constraints.push_back(
        {.kind = CompositionConstraintKind::hierarchy_containment,
         .severity = ConstraintSeverity::hard,
         .element_id = el.id,
         .other_id = std::string(kContentFrameId)});
  }

  auto solved = solve_composition(working, constraints, canvas_width_px,
                                  canvas_height_px);
  if (!solved) {
    return solved;
  }

  // Drop the synthetic content frame from the public solution.
  std::vector<LayoutElement> filtered;
  filtered.reserve(solved->elements.size());
  for (LayoutElement &el : solved->elements) {
    if (el.id == kContentFrameId) {
      continue;
    }
    filtered.push_back(std::move(el));
  }
  solved->elements = std::move(filtered);
  return solved;
}

} // namespace instantee::direction
