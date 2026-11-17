#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>
#include <vector>

namespace instantee::render_core {

// Editorial chart constructs lower onto shared DrawOp geometry only (IR
// evolution: no ad hoc IR node kinds here). Values always carry an explicit
// unit; scaling never divides by a zero domain.

enum class ChartUnit : std::uint8_t {
  count = 1,   // absolute counts (>= 0)
  percent = 2, // percentage points in [0, 100]
  ratio = 3,   // unit interval [0, 1]
};

enum class ChartKind : std::uint8_t {
  bar = 1,
  line = 2,
  comparison = 3,
  percent_meter = 4,
  counter = 5,
};

struct ChartSeries {
  ChartUnit unit = ChartUnit::count;
  std::vector<double> values{};
};

struct BarChartSpec {
  Rect2f plot{};
  ChartSeries series{};
  // Domain maximum used for bar height. Must be finite and >= 0. When 0, every
  // bar collapses to zero height (no division by zero).
  double domain_max = 0;
  float gap_px = 2.0f;
  Rgba8 bar_color{0, 0, 0, 255};
  Rgba8 baseline_color{180, 180, 180, 255};
};

struct LineChartSpec {
  Rect2f plot{};
  ChartSeries series{};
  double domain_max = 0;
  float stroke_width = 2.0f;
  Rgba8 line_color{0, 0, 0, 255};
};

struct ComparisonChartSpec {
  Rect2f plot{};
  ChartSeries left{};
  ChartSeries right{};
  double domain_max = 0;
  float gap_px = 2.0f;
  float pair_gap_px = 2.0f;
  Rgba8 left_color{0, 0, 0, 255};
  Rgba8 right_color{198, 255, 0, 255};
};

struct PercentMeterSpec {
  Rect2f track{};
  // Value interpreted in ChartUnit::percent ([0, 100]).
  double percent = 0;
  float corner_radius = 2.0f;
  Rgba8 track_color{230, 230, 230, 255};
  Rgba8 fill_color{0, 0, 0, 255};
};

struct CounterSpec {
  float cx = 0;
  float cy = 0;
  float radius = 12.0f;
  // Displayed as a single digit 0–9 after unit-aware quantization.
  double value = 0;
  ChartUnit unit = ChartUnit::count;
  Rgba8 fill{0, 0, 0, 255};
  Rgba8 ink{255, 255, 255, 255};
};

using ChartSpec = std::variant<BarChartSpec, LineChartSpec, ComparisonChartSpec,
                               PercentMeterSpec, CounterSpec>;

struct ChartScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{255, 255, 255, 255};
  std::vector<ChartSpec> charts{};
};

[[nodiscard]] constexpr std::string_view
chart_unit_id(ChartUnit unit) noexcept {
  switch (unit) {
  case ChartUnit::count:
    return "count";
  case ChartUnit::percent:
    return "percent";
  case ChartUnit::ratio:
    return "ratio";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
chart_kind_id(ChartKind kind) noexcept {
  switch (kind) {
  case ChartKind::bar:
    return "bar";
  case ChartKind::line:
    return "line";
  case ChartKind::comparison:
    return "comparison";
  case ChartKind::percent_meter:
    return "percent_meter";
  case ChartKind::counter:
    return "counter";
  }
  return "unknown";
}

[[nodiscard]] ChartKind chart_kind(const ChartSpec &spec) noexcept;

// Validates finite values and unit ranges (percent∈[0,100], ratio∈[0,1],
// count>=0).
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_chart_series(const ChartSeries &series);

// Linear blend of equal-length, same-unit series. t is clamped to [0, 1].
[[nodiscard]] std::expected<ChartSeries, diagnostics::Error>
interpolate_chart_series(const ChartSeries &from, const ChartSeries &to,
                         double t);

// Maps value into [0, 1] against domain_max. domain_max==0 → 0 (no division).
[[nodiscard]] std::expected<double, diagnostics::Error>
chart_unit_coverage(double value, double domain_max) noexcept;

// Quantize a unit-aware value to a display digit 0–9 for counter badges.
[[nodiscard]] std::expected<std::uint8_t, diagnostics::Error>
chart_counter_digit(double value, ChartUnit unit);

[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_chart(const ChartSpec &spec);

[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
compose_chart_pass(const ChartScene &scene);

[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_chart_scene(const ChartScene &scene);

} // namespace instantee::render_core
