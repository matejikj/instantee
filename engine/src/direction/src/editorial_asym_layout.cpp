#include <instantee/direction/editorial_asym_layout.hpp>

#include <instantee/direction/safe_area.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_asym_layout", message};
}

[[nodiscard]] bool finite_positive(double v) noexcept {
  return std::isfinite(v) && v > 0.0;
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
                                    double w, double h, double importance,
                                    bool keep_visible,
                                    std::optional<TextMetrics> text = {}) {
  LayoutElement el;
  el.id = std::move(id);
  el.x_px = x;
  el.y_px = y;
  el.width_px = w;
  el.height_px = h;
  el.importance = importance;
  el.keep_visible = keep_visible;
  el.text = std::move(text);
  return el;
}

[[nodiscard]] EditorialFocalPoint
compute_focal(const std::vector<LayoutElement> &elements, double content_x,
              double content_y, double content_w, double content_h) {
  EditorialFocalPoint focal;
  // Readable band: left-biased optical center covering the hero stack (short
  // and tall element counts) without hugging the safe-area edge.
  focal.band_x_px = content_x + content_w * 0.18;
  focal.band_y_px = content_y + content_h * 0.05;
  focal.band_width_px = content_w * 0.42;
  focal.band_height_px = content_h * 0.45;

  double weight_sum = 0.0;
  double wx = 0.0;
  double wy = 0.0;
  for (const LayoutElement &el : elements) {
    const double w = std::max(el.importance, 0.0);
    wx += w * (el.x_px + el.width_px * 0.5);
    wy += w * (el.y_px + el.height_px * 0.5);
    weight_sum += w;
  }
  if (weight_sum > 0.0) {
    focal.x_px = wx / weight_sum;
    focal.y_px = wy / weight_sum;
  } else {
    focal.x_px = content_x + content_w * 0.5;
    focal.y_px = content_y + content_h * 0.5;
  }
  focal.inside_readable_band = editorial_focal_is_readable(focal);
  return focal;
}

} // namespace

bool editorial_focal_is_readable(const EditorialFocalPoint &focal) noexcept {
  if (!(focal.band_width_px > 0.0 && focal.band_height_px > 0.0)) {
    return false;
  }
  return focal.x_px >= focal.band_x_px &&
         focal.x_px <= focal.band_x_px + focal.band_width_px &&
         focal.y_px >= focal.band_y_px &&
         focal.y_px <= focal.band_y_px + focal.band_height_px;
}

EditorialAsymRequest editorial_asym_request_from_style(
    const StyleBible &bible, std::uint32_t element_count,
    double canvas_width_px, double canvas_height_px) {
  EditorialAsymRequest req;
  req.canvas_width_px = canvas_width_px;
  req.canvas_height_px = canvas_height_px;
  req.safe_area = bible.safe_area();
  req.column_gap_px = spacing_or(bible, "column_gap", 32.0);
  req.stack_gap_px = spacing_or(bible, "stack", 16.0);
  req.accent_bar_height_px = spacing_or(bible, "accent_bar", 12.0);
  req.element_count = element_count;
  return req;
}

std::expected<EditorialAsymLayout, diagnostics::Error>
compose_editorial_asymmetrical(const EditorialAsymRequest &request) {
  if (!finite_positive(request.canvas_width_px) ||
      !finite_positive(request.canvas_height_px)) {
    return std::unexpected(make_invalid(
        "invalid_canvas", "Canvas dimensions must be finite and > 0"));
  }
  if (request.element_count < 2 || request.element_count > 5) {
    return std::unexpected(make_invalid(
        "unsupported_element_count",
        "Editorial asymmetrical templates support element_count in [2, 5]"));
  }
  if (!std::isfinite(request.column_gap_px) || request.column_gap_px < 0.0 ||
      !std::isfinite(request.stack_gap_px) || request.stack_gap_px < 0.0 ||
      !std::isfinite(request.accent_bar_height_px) ||
      request.accent_bar_height_px <= 0.0) {
    return std::unexpected(make_invalid(
        "invalid_spacing", "Spacing tokens must be finite and non-negative"));
  }
  if (!(request.primary_column_ratio > 0.5 &&
        request.primary_column_ratio <= 0.85) ||
      !(request.headline_overhang_ratio >= 0.0 &&
        request.headline_overhang_ratio < 0.35)) {
    return std::unexpected(make_invalid("invalid_grid_ratios",
                                        "primary_column_ratio must be in (0.5, "
                                        "0.85] and overhang in [0, 0.35)"));
  }

  auto content = content_safe_rect(request.safe_area, request.canvas_width_px,
                                   request.canvas_height_px);
  if (!content) {
    return std::unexpected(content.error());
  }
  if (content->width_px <= request.column_gap_px + 1.0 ||
      content->height_px <= 1.0) {
    return std::unexpected(make_invalid(
        "content_too_small", "Safe-area content rect cannot host the grid"));
  }

  const double usable_w = content->width_px - request.column_gap_px;
  const double primary_w = usable_w * request.primary_column_ratio;
  const double secondary_w = usable_w - primary_w;
  if (!(primary_w > 0.0 && secondary_w > 0.0)) {
    return std::unexpected(make_invalid(
        "degenerate_columns", "Asymmetrical columns must have positive width"));
  }

  EditorialAsymLayout out;
  out.content_x_px = content->x_px;
  out.content_y_px = content->y_px;
  out.content_width_px = content->width_px;
  out.content_height_px = content->height_px;
  out.primary_column_x_px = content->x_px;
  out.primary_column_width_px = primary_w;
  out.secondary_column_x_px = content->x_px + primary_w + request.column_gap_px;
  out.secondary_column_width_px = secondary_w;

  const double overhang =
      std::min(content->width_px * request.headline_overhang_ratio,
               secondary_w + request.column_gap_px);
  const double headline_w = std::min(primary_w + overhang, content->width_px);
  const double headline_h = std::min(160.0, content->height_px * 0.12);
  const double body_h = std::min(200.0, content->height_px * 0.14);
  const double callout_h = std::min(120.0, content->height_px * 0.08);
  const double photo_h = std::min(420.0, content->height_px * 0.28);

  double cursor_y = content->y_px;
  out.elements.push_back(make_el("headline", content->x_px, cursor_y,
                                 headline_w, headline_h, 0.98, true,
                                 TextMetrics{.width_px = headline_w * 0.92,
                                             .height_px = headline_h * 0.75,
                                             .ascent_px = headline_h * 0.6,
                                             .descent_px = headline_h * 0.15}));
  cursor_y += headline_h + request.stack_gap_px;

  if (request.element_count >= 2) {
    out.elements.push_back(make_el("accent_bar", content->x_px, cursor_y,
                                   std::min(160.0, primary_w * 0.35),
                                   request.accent_bar_height_px, 0.4, false));
    cursor_y += request.accent_bar_height_px + request.stack_gap_px;
  }

  if (request.element_count >= 3) {
    out.elements.push_back(make_el("body", content->x_px, cursor_y, primary_w,
                                   body_h, 0.82, true,
                                   TextMetrics{.width_px = primary_w * 0.95,
                                               .height_px = body_h * 0.8,
                                               .ascent_px = body_h * 0.64,
                                               .descent_px = body_h * 0.16}));
    cursor_y += body_h + request.stack_gap_px;
  }

  if (request.element_count >= 4) {
    // Callout sits in the secondary column — grid break / hierarchy shift.
    out.elements.push_back(make_el(
        "callout", out.secondary_column_x_px, content->y_px + headline_h * 0.35,
        secondary_w, callout_h, 0.7, true,
        TextMetrics{.width_px = secondary_w * 0.9,
                    .height_px = callout_h * 0.7,
                    .ascent_px = callout_h * 0.55,
                    .descent_px = callout_h * 0.15}));
  }

  if (request.element_count >= 5) {
    const double photo_y =
        std::max(cursor_y, content->y_px + content->height_px * 0.42);
    out.elements.push_back(make_el("photo", content->x_px, photo_y,
                                   content->width_px, photo_h, 0.75, true));
  }

  // Keep hierarchy: children conceptually under the content frame via
  // containment constraints when solved; here we only emit placed elements.
  if (out.elements.back().y_px + out.elements.back().height_px >
      content->y_px + content->height_px) {
    return std::unexpected(make_invalid(
        "vertical_overflow",
        "Editorial asymmetrical stack exceeds the content safe rect"));
  }

  out.focal = compute_focal(out.elements, content->x_px, content->y_px,
                            content->width_px, content->height_px);
  if (!out.focal.inside_readable_band) {
    return std::unexpected(
        make_invalid("unreadable_focal_point",
                     "Importance-weighted focal point left the readable band"));
  }
  return out;
}

} // namespace instantee::direction
