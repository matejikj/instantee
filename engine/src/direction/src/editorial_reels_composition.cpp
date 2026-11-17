#include <instantee/direction/editorial_reels_composition.hpp>

#include <cmath>
#include <string>
#include <string_view>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_reels_composition", message};
}

[[nodiscard]] bool finite_positive(double v) noexcept {
  return std::isfinite(v) && v > 0.0;
}

[[nodiscard]] bool finite_nonneg(double v) noexcept {
  return std::isfinite(v) && v >= 0.0;
}

[[nodiscard]] double spacing_or(const StyleBible &bible, std::string_view id,
                                double fallback) {
  const auto it = bible.spacing().find(std::string(id));
  if (it == bible.spacing().end()) {
    return fallback;
  }
  return it->second.px;
}

[[nodiscard]] LayoutElement make_el(std::string id, double x, double y,
                                    double w, double h, double importance) {
  LayoutElement el;
  el.id = std::move(id);
  el.x_px = x;
  el.y_px = y;
  el.width_px = w;
  el.height_px = h;
  el.importance = importance;
  return el;
}

[[nodiscard]] bool element_inside_content(const LayoutElement &el,
                                          const ContentSafeRect &c) noexcept {
  constexpr double eps = 1e-6;
  return el.x_px + eps >= c.x_px && el.y_px + eps >= c.y_px &&
         el.x_px + el.width_px <= c.x_px + c.width_px + eps &&
         el.y_px + el.height_px <= c.y_px + c.height_px + eps;
}

} // namespace

ReelsCompositionRequest reels_composition_request_from_style(
    const StyleBible &bible, double canvas_width_px, double canvas_height_px) {
  ReelsCompositionRequest req;
  req.canvas_width_px = canvas_width_px;
  req.canvas_height_px = canvas_height_px;
  req.safe_area = bible.safe_area();
  req.stack_gap_px = spacing_or(bible, "stack", 16.0);
  req.gutter_px = spacing_or(bible, "gutter", 24.0);
  req.style_package_id = bible.package_id();
  req.style_version = bible.version();
  return req;
}

std::expected<ReelsCompositionLayout, diagnostics::Error>
compose_editorial_reels(const ReelsCompositionRequest &request) {
  if (!finite_positive(request.canvas_width_px) ||
      !finite_positive(request.canvas_height_px)) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "Canvas dimensions must be finite and > 0"));
  }
  if (!finite_nonneg(request.stack_gap_px) ||
      !finite_nonneg(request.gutter_px)) {
    return std::unexpected(make_invalid(
        "invalid_spacing", "Spacing values must be finite and >= 0"));
  }

  const auto &r = request.ratios;
  if (!finite_positive(r.top_headline) || !finite_positive(r.callout) ||
      !finite_positive(r.chart) || !finite_positive(r.callout_width)) {
    return std::unexpected(make_invalid(
        "invalid_ratios", "Composition ratios must be finite and > 0"));
  }
  if (r.callout_width > 1.0) {
    return std::unexpected(make_invalid("invalid_callout_width",
                                        "Callout width ratio must be <= 1"));
  }

  const double reserved = r.top_headline + r.callout + r.chart;
  if (!(reserved < 1.0)) {
    return std::unexpected(make_invalid(
        "ratios_overflow",
        "Headline+callout+chart ratios must leave room for bottom_space"));
  }
  const double bottom_ratio = 1.0 - reserved;

  const auto content = content_safe_rect(
      request.safe_area, request.canvas_width_px, request.canvas_height_px);
  if (!content.has_value()) {
    return std::unexpected(content.error());
  }
  const ContentSafeRect &c = *content;

  // Three stack gaps separate the four vertical bands.
  const double gap_budget = 3.0 * request.stack_gap_px;
  if (gap_budget >= c.height_px) {
    return std::unexpected(make_invalid(
        "spacing_overflow", "Stack gaps leave no room for composition slots"));
  }
  const double usable_h = c.height_px - gap_budget;

  const double h_headline = usable_h * r.top_headline;
  const double h_callout = usable_h * r.callout;
  const double h_chart = usable_h * r.chart;
  const double h_bottom = usable_h * bottom_ratio;

  const double y_headline = c.y_px;
  const double y_callout = y_headline + h_headline + request.stack_gap_px;
  const double y_chart = y_callout + h_callout + request.stack_gap_px;
  const double y_bottom = y_chart + h_chart + request.stack_gap_px;

  const double callout_w = c.width_px * r.callout_width;
  // Right-align the callout chip with a token gutter from the content edge.
  double callout_x = c.x_px + c.width_px - callout_w;
  if (request.gutter_px > 0.0 && callout_w + request.gutter_px <= c.width_px) {
    callout_x -= request.gutter_px;
  }
  if (callout_x < c.x_px) {
    callout_x = c.x_px;
  }

  ReelsCompositionLayout out;
  out.content = c;
  out.style_package_id = request.style_package_id;
  out.style_version = request.style_version;
  out.elements.reserve(4);
  out.elements.push_back(make_el(std::string(reels_composition_slot_id(
                                     ReelsCompositionSlot::top_headline)),
                                 c.x_px, y_headline, c.width_px, h_headline,
                                 1.0));
  out.elements.push_back(make_el(
      std::string(reels_composition_slot_id(ReelsCompositionSlot::callout)),
      callout_x, y_callout, callout_w, h_callout, 0.85));
  out.elements.push_back(make_el(
      std::string(reels_composition_slot_id(ReelsCompositionSlot::chart)),
      c.x_px, y_chart, c.width_px, h_chart, 0.9));
  out.elements.push_back(make_el(std::string(reels_composition_slot_id(
                                     ReelsCompositionSlot::bottom_space)),
                                 c.x_px, y_bottom, c.width_px, h_bottom, 0.4));
  return out;
}

bool reels_composition_respects_safe_area(
    const ReelsCompositionLayout &layout) {
  for (const auto &el : layout.elements) {
    if (!element_inside_content(el, layout.content)) {
      return false;
    }
  }
  return true;
}

} // namespace instantee::direction
