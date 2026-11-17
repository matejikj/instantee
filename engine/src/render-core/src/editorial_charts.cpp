#include <instantee/render_core/editorial_charts.hpp>

#include <instantee/render_core/graphic_shapes.hpp>
#include <instantee/render_core/primitives.hpp>
#include <instantee/render_core/renderer.hpp>

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_charts", message};
}

[[nodiscard]] double clamp01(double t) noexcept {
  return std::min(1.0, std::max(0.0, t));
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = {.x = x0, .y = y0, .width = 0, .height = 0},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] DrawOp make_fill_rect(Rect2f bounds, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rect, .rect = bounds, .color = color};
}

[[nodiscard]] DrawOp make_fill_rounded(Rect2f bounds, float radius,
                                       Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::fill_rounded_rect,
                .rect = bounds,
                .color = color,
                .corner_radius = radius};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_plot(const Rect2f &plot) {
  if (!is_valid_rect(plot) || !std::isfinite(plot.x) ||
      !std::isfinite(plot.y)) {
    return std::unexpected(make_invalid(
        "invalid_plot", "chart plot must be finite with size > 0"));
  }
  return {};
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_domain(double domain_max) {
  if (!std::isfinite(domain_max) || domain_max < 0.0) {
    return std::unexpected(make_invalid(
        "invalid_domain", "chart domain_max must be finite and >= 0"));
  }
  return {};
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_bar(const BarChartSpec &spec) {
  if (auto ok = validate_plot(spec.plot); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_domain(spec.domain_max); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_chart_series(spec.series); !ok) {
    return std::unexpected(ok.error());
  }
  if (spec.series.values.empty()) {
    return std::unexpected(
        make_invalid("empty_series", "bar chart requires at least one value"));
  }
  if (!std::isfinite(spec.gap_px) || spec.gap_px < 0.0f) {
    return std::unexpected(
        make_invalid("invalid_gap", "bar gap_px must be finite and >= 0"));
  }

  const auto n = static_cast<float>(spec.series.values.size());
  const float total_gap = spec.gap_px * (n + 1.0f);
  if (!(spec.plot.width > total_gap)) {
    return std::unexpected(make_invalid(
        "invalid_gap", "bar gaps leave no positive width for bars"));
  }
  const float bar_w = (spec.plot.width - total_gap) / n;
  if (!(bar_w > 0.0f)) {
    return std::unexpected(
        make_invalid("invalid_bar_width", "computed bar width must be > 0"));
  }

  std::vector<DrawOp> ops;
  // Baseline.
  ops.push_back(make_line(spec.plot.x, spec.plot.y + spec.plot.height,
                          spec.plot.x + spec.plot.width,
                          spec.plot.y + spec.plot.height, 1.0f,
                          spec.baseline_color));

  for (std::size_t i = 0; i < spec.series.values.size(); ++i) {
    auto cover = chart_unit_coverage(spec.series.values[i], spec.domain_max);
    if (!cover) {
      return std::unexpected(cover.error());
    }
    const float h = static_cast<float>(*cover) * spec.plot.height;
    if (!(h > 0.0f)) {
      continue;
    }
    Rect2f bar{.x = spec.plot.x + spec.gap_px +
                    static_cast<float>(i) * (bar_w + spec.gap_px),
               .y = spec.plot.y + spec.plot.height - h,
               .width = bar_w,
               .height = h};
    ops.push_back(make_fill_rect(bar, spec.bar_color));
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_line(const LineChartSpec &spec) {
  if (auto ok = validate_plot(spec.plot); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_domain(spec.domain_max); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_chart_series(spec.series); !ok) {
    return std::unexpected(ok.error());
  }
  if (spec.series.values.size() < 2) {
    return std::unexpected(make_invalid(
        "empty_series", "line chart requires at least two values"));
  }
  if (!std::isfinite(spec.stroke_width) || !(spec.stroke_width > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_stroke", "line stroke_width must be finite and > 0"));
  }

  const auto n = spec.series.values.size();
  std::vector<float> xs(n);
  std::vector<float> ys(n);
  for (std::size_t i = 0; i < n; ++i) {
    auto cover = chart_unit_coverage(spec.series.values[i], spec.domain_max);
    if (!cover) {
      return std::unexpected(cover.error());
    }
    const float u = static_cast<float>(i) / static_cast<float>(n - 1); // n>=2
    xs[i] = spec.plot.x + u * spec.plot.width;
    ys[i] = spec.plot.y + spec.plot.height -
            static_cast<float>(*cover) * spec.plot.height;
  }

  std::vector<DrawOp> ops;
  for (std::size_t i = 1; i < n; ++i) {
    ops.push_back(make_line(xs[i - 1], ys[i - 1], xs[i], ys[i],
                            spec.stroke_width, spec.line_color));
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_comparison(const ComparisonChartSpec &spec) {
  if (auto ok = validate_plot(spec.plot); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_domain(spec.domain_max); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_chart_series(spec.left); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_chart_series(spec.right); !ok) {
    return std::unexpected(ok.error());
  }
  if (spec.left.unit != spec.right.unit) {
    return std::unexpected(make_invalid(
        "unit_mismatch", "comparison series must share the same unit"));
  }
  if (spec.left.values.size() != spec.right.values.size() ||
      spec.left.values.empty()) {
    return std::unexpected(
        make_invalid("length_mismatch",
                     "comparison series must be non-empty and equal length"));
  }
  if (!std::isfinite(spec.gap_px) || spec.gap_px < 0.0f ||
      !std::isfinite(spec.pair_gap_px) || spec.pair_gap_px < 0.0f) {
    return std::unexpected(
        make_invalid("invalid_gap", "comparison gaps must be finite and >= 0"));
  }

  const auto pairs = static_cast<float>(spec.left.values.size());
  const float group_gaps = spec.gap_px * (pairs + 1.0f);
  if (!(spec.plot.width > group_gaps)) {
    return std::unexpected(
        make_invalid("invalid_gap", "comparison gaps leave no positive width"));
  }
  const float group_w = (spec.plot.width - group_gaps) / pairs;
  const float bar_w = (group_w - spec.pair_gap_px) * 0.5f;
  if (!(bar_w > 0.0f)) {
    return std::unexpected(
        make_invalid("invalid_bar_width", "comparison bar width must be > 0"));
  }

  std::vector<DrawOp> ops;
  for (std::size_t i = 0; i < spec.left.values.size(); ++i) {
    auto left_c = chart_unit_coverage(spec.left.values[i], spec.domain_max);
    auto right_c = chart_unit_coverage(spec.right.values[i], spec.domain_max);
    if (!left_c) {
      return std::unexpected(left_c.error());
    }
    if (!right_c) {
      return std::unexpected(right_c.error());
    }
    const float group_x = spec.plot.x + spec.gap_px +
                          static_cast<float>(i) * (group_w + spec.gap_px);
    const float lh = static_cast<float>(*left_c) * spec.plot.height;
    const float rh = static_cast<float>(*right_c) * spec.plot.height;
    if (lh > 0.0f) {
      ops.push_back(
          make_fill_rect(Rect2f{.x = group_x,
                                .y = spec.plot.y + spec.plot.height - lh,
                                .width = bar_w,
                                .height = lh},
                         spec.left_color));
    }
    if (rh > 0.0f) {
      ops.push_back(
          make_fill_rect(Rect2f{.x = group_x + bar_w + spec.pair_gap_px,
                                .y = spec.plot.y + spec.plot.height - rh,
                                .width = bar_w,
                                .height = rh},
                         spec.right_color));
    }
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_percent_meter(const PercentMeterSpec &spec) {
  if (auto ok = validate_plot(spec.track); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(spec.percent) || spec.percent < 0.0 ||
      spec.percent > 100.0) {
    return std::unexpected(make_invalid(
        "invalid_percent", "percent meter value must be finite in [0, 100]"));
  }
  if (!std::isfinite(spec.corner_radius) || spec.corner_radius < 0.0f) {
    return std::unexpected(make_invalid(
        "invalid_corner_radius", "corner_radius must be finite and >= 0"));
  }

  // Coverage against a fixed 100% domain — never divides by zero.
  auto cover = chart_unit_coverage(spec.percent, 100.0);
  if (!cover) {
    return std::unexpected(cover.error());
  }

  std::vector<DrawOp> ops;
  if (spec.corner_radius > 0.0f) {
    ops.push_back(
        make_fill_rounded(spec.track, spec.corner_radius, spec.track_color));
  } else {
    ops.push_back(make_fill_rect(spec.track, spec.track_color));
  }
  const float fill_w = static_cast<float>(*cover) * spec.track.width;
  if (fill_w > 0.0f) {
    Rect2f fill = spec.track;
    fill.width = fill_w;
    if (spec.corner_radius > 0.0f) {
      ops.push_back(
          make_fill_rounded(fill, spec.corner_radius, spec.fill_color));
    } else {
      ops.push_back(make_fill_rect(fill, spec.fill_color));
    }
  }
  return ops;
}

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_counter(const CounterSpec &spec) {
  auto digit = chart_counter_digit(spec.value, spec.unit);
  if (!digit) {
    return std::unexpected(digit.error());
  }
  if (!std::isfinite(spec.cx) || !std::isfinite(spec.cy) ||
      !std::isfinite(spec.radius) || !(spec.radius > 0.0f)) {
    return std::unexpected(make_invalid(
        "invalid_counter", "counter center/radius must be finite; radius > 0"));
  }
  // Reuse the numeric callout vocabulary (shared primitives, no new renderer).
  return lower_graphic_shape(NumericCalloutShape{
      .cx = spec.cx,
      .cy = spec.cy,
      .radius = spec.radius,
      .digit = *digit,
      .stem_x = spec.cx,
      .stem_y = spec.cy,
      .fill = spec.fill,
      .ink = spec.ink,
      .stem_color = spec.fill,
  });
}

} // namespace

ChartKind chart_kind(const ChartSpec &spec) noexcept {
  return std::visit(
      [](const auto &value) -> ChartKind {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, BarChartSpec>) {
          return ChartKind::bar;
        } else if constexpr (std::is_same_v<T, LineChartSpec>) {
          return ChartKind::line;
        } else if constexpr (std::is_same_v<T, ComparisonChartSpec>) {
          return ChartKind::comparison;
        } else if constexpr (std::is_same_v<T, PercentMeterSpec>) {
          return ChartKind::percent_meter;
        } else {
          return ChartKind::counter;
        }
      },
      spec);
}

std::expected<void, diagnostics::Error>
validate_chart_series(const ChartSeries &series) {
  for (double v : series.values) {
    if (!std::isfinite(v)) {
      return std::unexpected(
          make_invalid("non_finite_value", "chart values must be finite"));
    }
    switch (series.unit) {
    case ChartUnit::count:
      if (v < 0.0) {
        return std::unexpected(
            make_invalid("invalid_count", "count values must be >= 0"));
      }
      break;
    case ChartUnit::percent:
      if (v < 0.0 || v > 100.0) {
        return std::unexpected(make_invalid(
            "invalid_percent", "percent values must be in [0, 100]"));
      }
      break;
    case ChartUnit::ratio:
      if (v < 0.0 || v > 1.0) {
        return std::unexpected(
            make_invalid("invalid_ratio", "ratio values must be in [0, 1]"));
      }
      break;
    }
  }
  return {};
}

std::expected<ChartSeries, diagnostics::Error>
interpolate_chart_series(const ChartSeries &from, const ChartSeries &to,
                         double t) {
  if (from.unit != to.unit) {
    return std::unexpected(make_invalid(
        "unit_mismatch", "interpolated series must share the same unit"));
  }
  if (from.values.size() != to.values.size()) {
    return std::unexpected(make_invalid(
        "length_mismatch", "interpolated series must have equal length"));
  }
  if (auto ok = validate_chart_series(from); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_chart_series(to); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(t)) {
    return std::unexpected(
        make_invalid("invalid_t", "interpolation t must be finite"));
  }

  const double u = clamp01(t);
  ChartSeries out;
  out.unit = from.unit;
  out.values.resize(from.values.size());
  for (std::size_t i = 0; i < from.values.size(); ++i) {
    out.values[i] = from.values[i] + (to.values[i] - from.values[i]) * u;
  }
  // Re-validate after blend (percent/ratio stay in range when endpoints do).
  if (auto ok = validate_chart_series(out); !ok) {
    return std::unexpected(ok.error());
  }
  return out;
}

std::expected<double, diagnostics::Error>
chart_unit_coverage(double value, double domain_max) noexcept {
  if (!std::isfinite(value) || !std::isfinite(domain_max) || domain_max < 0.0) {
    return std::unexpected(
        make_invalid("invalid_coverage_args",
                     "coverage requires finite value and domain_max >= 0"));
  }
  if (domain_max == 0.0) {
    // Explicit zero-domain policy: no division; coverage is zero.
    return 0.0;
  }
  return clamp01(value / domain_max);
}

std::expected<std::uint8_t, diagnostics::Error>
chart_counter_digit(double value, ChartUnit unit) {
  ChartSeries probe{.unit = unit, .values = {value}};
  if (auto ok = validate_chart_series(probe); !ok) {
    return std::unexpected(ok.error());
  }
  double scaled = 0.0;
  switch (unit) {
  case ChartUnit::count:
    scaled = value;
    break;
  case ChartUnit::percent:
    scaled = value / 10.0; // 0..100 → 0..10
    break;
  case ChartUnit::ratio:
    scaled = value * 10.0; // 0..1 → 0..10
    break;
  }
  const auto digit = static_cast<int>(std::llround(scaled));
  return static_cast<std::uint8_t>(std::clamp(digit, 0, 9));
}

std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_chart(const ChartSpec &spec) {
  return std::visit(
      [](const auto &value)
          -> std::expected<std::vector<DrawOp>, diagnostics::Error> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, BarChartSpec>) {
          return lower_bar(value);
        } else if constexpr (std::is_same_v<T, LineChartSpec>) {
          return lower_line(value);
        } else if constexpr (std::is_same_v<T, ComparisonChartSpec>) {
          return lower_comparison(value);
        } else if constexpr (std::is_same_v<T, PercentMeterSpec>) {
          return lower_percent_meter(value);
        } else {
          return lower_counter(value);
        }
      },
      spec);
}

std::expected<PassDesc, diagnostics::Error>
compose_chart_pass(const ChartScene &scene) {
  if (scene.width_px == 0 || scene.height_px == 0) {
    return std::unexpected(
        make_invalid("invalid_extent", "chart scene width/height must be > 0"));
  }
  if (scene.charts.empty()) {
    return std::unexpected(
        make_invalid("empty_scene", "chart scene has no charts"));
  }

  PassDesc pass;
  pass.width_px = scene.width_px;
  pass.height_px = scene.height_px;
  pass.color_target = 1;
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = scene.clear_color});

  for (const auto &chart : scene.charts) {
    auto lowered = lower_chart(chart);
    if (!lowered) {
      return std::unexpected(lowered.error());
    }
    for (const auto &op : *lowered) {
      if (op.kind != DrawOpKind::fill_rect &&
          op.kind != DrawOpKind::fill_ellipse &&
          op.kind != DrawOpKind::fill_rounded_rect &&
          op.kind != DrawOpKind::stroke_line) {
        return std::unexpected(
            make_invalid("unsupported_primitive",
                         "chart lowering escaped shared DrawOp kinds"));
      }
      pass.ops.push_back(op);
    }
  }

  if (auto valid = validate_pass(pass); !valid) {
    return std::unexpected(valid.error());
  }
  return pass;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_chart_scene(const ChartScene &scene) {
  auto pass = compose_chart_pass(scene);
  if (!pass) {
    return std::unexpected(pass.error());
  }
  Rgba8Image image;
  if (auto ok = rasterize_pass(image, *pass); !ok) {
    return std::unexpected(ok.error());
  }
  return image;
}

} // namespace instantee::render_core
