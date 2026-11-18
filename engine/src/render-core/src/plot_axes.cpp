#include <instantee/render_core/plot_axes.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "plot_axes", message};
}

[[nodiscard]] bool finite_rect(const Rect2f &r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && is_valid_rect(r);
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_domain(const AxisDomain &d, std::string_view which) {
  if (!std::isfinite(d.min) || !std::isfinite(d.max)) {
    return std::unexpected(make_invalid(
        "invalid_domain", std::string{which} + " domain must be finite"));
  }
  if (!(d.max > d.min)) {
    return std::unexpected(make_invalid(
        "invalid_domain",
        std::string{which} + " domain max must be greater than min"));
  }
  return {};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_spec(const AxesSpec &spec) {
  if (!finite_rect(spec.plot)) {
    return std::unexpected(
        make_invalid("invalid_plot", "axes plot must be finite with size > 0"));
  }
  if (auto ok = validate_domain(spec.x, "x"); !ok) {
    return ok;
  }
  if (auto ok = validate_domain(spec.y, "y"); !ok) {
    return ok;
  }
  if (!std::isfinite(spec.label_min_gap_px) || spec.label_min_gap_px < 0.0f) {
    return std::unexpected(make_invalid(
        "invalid_gap", "label_min_gap_px must be finite and >= 0"));
  }
  if (spec.target_ticks == 0 || spec.max_ticks == 0 ||
      spec.target_ticks > spec.max_ticks) {
    return std::unexpected(make_invalid(
        "invalid_tick_budget",
        "target_ticks must be in [1, max_ticks] and max_ticks > 0"));
  }
  if (!std::isfinite(spec.axis_stroke_px) || !(spec.axis_stroke_px > 0.0f) ||
      !std::isfinite(spec.tick_length_px) || spec.tick_length_px < 0.0f ||
      !std::isfinite(spec.grid_stroke_px) || !(spec.grid_stroke_px > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_stroke", "axis/grid stroke widths must be finite and > 0"));
  }
  return {};
}

// Nice step from Wilkinson-style candidates around range/target.
[[nodiscard]] double nice_step(double range, std::uint32_t target) noexcept {
  if (!(range > 0.0) || target == 0) {
    return 1.0;
  }
  const double rough = range / static_cast<double>(target);
  const double expv = std::floor(std::log10(rough));
  const double base = std::pow(10.0, expv);
  const double frac = rough / base;
  double nice_frac = 1.0;
  if (frac <= 1.5) {
    nice_frac = 1.0;
  } else if (frac <= 3.0) {
    nice_frac = 2.0;
  } else if (frac <= 7.0) {
    nice_frac = 5.0;
  } else {
    nice_frac = 10.0;
  }
  return nice_frac * base;
}

[[nodiscard]] std::string format_tick(double value, const std::string &unit) {
  char buf[64];
  const double av = std::fabs(value);
  if (av >= 100.0 || (av > 0.0 && av < 0.01)) {
    std::snprintf(buf, sizeof(buf), "%.4g", value);
  } else {
    std::snprintf(buf, sizeof(buf), "%.4g", value);
  }
  std::string out{buf};
  if (!unit.empty()) {
    out.push_back(' ');
    out += unit;
  }
  return out;
}

[[nodiscard]] float estimate_label_width_px(std::string_view text) noexcept {
  // Monospace-ish estimate for collision only (not typography metrics).
  constexpr float kChar = 7.0f;
  return std::max(8.0f, static_cast<float>(text.size()) * kChar);
}

[[nodiscard]] bool aabbs_too_close(const Rect2f &a, const Rect2f &b,
                                   float gap) noexcept {
  const float ax1 = a.x + a.width;
  const float ay1 = a.y + a.height;
  const float bx1 = b.x + b.width;
  const float by1 = b.y + b.height;
  // Expand each box by gap/2 so a gap violation counts as overlap.
  const float pad = gap * 0.5f;
  const bool sep = ax1 + pad <= b.x - pad || bx1 + pad <= a.x - pad ||
                   ay1 + pad <= b.y - pad || by1 + pad <= a.y - pad;
  return !sep;
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = Rect2f{.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] std::vector<AxisTick>
build_ticks(const AxisDomain &domain, float pixel_min, float pixel_max,
            bool vertical, float label_anchor_x, float label_anchor_y,
            std::uint32_t target, std::uint32_t max_ticks) {
  std::vector<AxisTick> ticks;
  const double range = domain.max - domain.min;
  const double step = nice_step(range, target);
  if (!(step > 0.0) || !std::isfinite(step)) {
    return ticks;
  }
  const double start = std::ceil(domain.min / step) * step;
  const float span = pixel_max - pixel_min;

  for (double v = start; v <= domain.max + step * 0.5; v += step) {
    if (v < domain.min - step * 1e-9) {
      continue;
    }
    if (v > domain.max + step * 1e-9) {
      break;
    }
    if (ticks.size() >= max_ticks) {
      break;
    }
    const double u = (v - domain.min) / range;
    AxisTick tick;
    tick.value = v;
    tick.pixel = pixel_min + static_cast<float>(u) * span;
    tick.label = format_tick(v, domain.unit);
    const float lw = estimate_label_width_px(tick.label);
    constexpr float lh = 12.0f;
    if (vertical) {
      tick.label_bounds = Rect2f{.x = label_anchor_x - lw - 2.0f,
                                 .y = tick.pixel - lh * 0.5f,
                                 .width = lw,
                                 .height = lh};
    } else {
      tick.label_bounds = Rect2f{.x = tick.pixel - lw * 0.5f,
                                 .y = label_anchor_y + 2.0f,
                                 .width = lw,
                                 .height = lh};
    }
    ticks.push_back(std::move(tick));
  }
  return ticks;
}

void detect_collisions(std::span<const AxisTick> ticks, float gap,
                       std::string_view axis, std::vector<AxesWarning> &out) {
  for (std::size_t i = 1; i < ticks.size(); ++i) {
    if (aabbs_too_close(ticks[i - 1].label_bounds, ticks[i].label_bounds,
                        gap)) {
      out.push_back(
          AxesWarning{.code = "axes.label_collision",
                      .message = std::string{axis} +
                                 " tick labels overlap or violate min gap"});
      return; // one warning per axis is enough
    }
  }
}

} // namespace

std::expected<PlotPoint, diagnostics::Error>
map_data_to_plot(const AxesSpec &axes, double data_x, double data_y) {
  if (auto ok = validate_spec(axes); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(data_x) || !std::isfinite(data_y)) {
    return std::unexpected(
        make_invalid("invalid_point", "data point must be finite"));
  }
  const double ux = (data_x - axes.x.min) / (axes.x.max - axes.x.min);
  const double uy = (data_y - axes.y.min) / (axes.y.max - axes.y.min);
  PlotPoint p;
  p.x = axes.plot.x + static_cast<float>(ux) * axes.plot.width;
  // Data +y maps toward the top of the plot (smaller pixel y).
  p.y = axes.plot.y + axes.plot.height -
        static_cast<float>(uy) * axes.plot.height;
  return p;
}

std::expected<PlotPoint, diagnostics::Error>
map_plot_to_data(const AxesSpec &axes, float plot_x, float plot_y) {
  if (auto ok = validate_spec(axes); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(plot_x) || !std::isfinite(plot_y)) {
    return std::unexpected(
        make_invalid("invalid_point", "plot point must be finite"));
  }
  const double ux =
      static_cast<double>((plot_x - axes.plot.x) / axes.plot.width);
  const double uy = static_cast<double>(
      (axes.plot.y + axes.plot.height - plot_y) / axes.plot.height);
  PlotPoint p;
  p.x = static_cast<float>(axes.x.min + ux * (axes.x.max - axes.x.min));
  p.y = static_cast<float>(axes.y.min + uy * (axes.y.max - axes.y.min));
  return p;
}

std::expected<AxesLayout, diagnostics::Error>
layout_axes(const AxesSpec &spec) {
  if (auto ok = validate_spec(spec); !ok) {
    return std::unexpected(ok.error());
  }

  AxesLayout layout;
  const float x0 = spec.plot.x;
  const float y0 = spec.plot.y;
  const float x1 = spec.plot.x + spec.plot.width;
  const float y1 = spec.plot.y + spec.plot.height;

  layout.x_ticks = build_ticks(spec.x, x0, x1, /*vertical=*/false, x0, y1,
                               spec.target_ticks, spec.max_ticks);
  layout.y_ticks = build_ticks(spec.y, y1, y0, /*vertical=*/true, x0, y0,
                               spec.target_ticks, spec.max_ticks);
  // Note: y ticks pixel from y1→y0 so increasing data maps upward (decreasing
  // pixel). build_ticks uses linear u from min→max across pixel_min→pixel_max.

  detect_collisions(layout.x_ticks, spec.label_min_gap_px, "x",
                    layout.warnings);
  detect_collisions(layout.y_ticks, spec.label_min_gap_px, "y",
                    layout.warnings);

  if (spec.show_grid) {
    for (const auto &t : layout.x_ticks) {
      layout.ops.push_back(make_line(t.pixel, y0, t.pixel, y1,
                                     spec.grid_stroke_px, spec.grid_color));
    }
    for (const auto &t : layout.y_ticks) {
      layout.ops.push_back(make_line(x0, t.pixel, x1, t.pixel,
                                     spec.grid_stroke_px, spec.grid_color));
    }
  }

  // Axes: left + bottom.
  layout.ops.push_back(
      make_line(x0, y0, x0, y1, spec.axis_stroke_px, spec.axis_color));
  layout.ops.push_back(
      make_line(x0, y1, x1, y1, spec.axis_stroke_px, spec.axis_color));

  for (const auto &t : layout.x_ticks) {
    layout.ops.push_back(make_line(t.pixel, y1, t.pixel,
                                   y1 + spec.tick_length_px,
                                   spec.axis_stroke_px, spec.axis_color));
  }
  for (const auto &t : layout.y_ticks) {
    layout.ops.push_back(make_line(x0 - spec.tick_length_px, t.pixel, x0,
                                   t.pixel, spec.axis_stroke_px,
                                   spec.axis_color));
  }

  (void)spec.label_color; // reserved for future text DrawOp kind
  return layout;
}

} // namespace instantee::render_core
