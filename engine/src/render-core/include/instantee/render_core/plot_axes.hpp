#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Data-space domain for one axis. Changing min/max remaps presentation only —
// callers keep their sample arrays untouched.
struct AxisDomain {
  double min = 0.0;
  double max = 1.0;
  // Unit suffix appended to tick labels when non-empty (e.g. "m", "s").
  std::string unit{};
};

struct PlotPoint {
  float x = 0;
  float y = 0;
};

struct AxesSpec {
  // Plot rectangle in pass pixel space (origin top-left, +y down).
  Rect2f plot{};
  AxisDomain x{};
  AxisDomain y{};
  // Soft minimum gap between adjacent tick label AABBs. Violations warn.
  float label_min_gap_px = 4.0f;
  // Approximate target tick count per axis before nice-step rounding.
  std::uint32_t target_ticks = 5;
  std::uint32_t max_ticks = 16;
  bool show_grid = true;
  float axis_stroke_px = 1.5f;
  float tick_length_px = 5.0f;
  float grid_stroke_px = 1.0f;
  Rgba8 axis_color{40, 40, 40, 255};
  Rgba8 grid_color{210, 210, 210, 255};
  Rgba8 label_color{40, 40, 40, 255};
};

struct AxisTick {
  double value = 0.0;
  // Pixel position along the plot edge (x for horizontal, y for vertical).
  float pixel = 0.0f;
  std::string label{};
  // Approximate label AABB in pass space (for collision checks / goldens).
  Rect2f label_bounds{};
};

struct AxesWarning {
  std::string code{};
  std::string message{};
};

struct AxesLayout {
  std::vector<AxisTick> x_ticks{};
  std::vector<AxisTick> y_ticks{};
  std::vector<DrawOp> ops{}; // grid (optional) + axes + ticks
  std::vector<AxesWarning> warnings{};
};

// Map a data-space point into the plot rectangle. Y is flipped so data +y is
// visually up while pass +y grows down. Rejects non-finite domains/points and
// zero-width domains.
[[nodiscard]] std::expected<PlotPoint, diagnostics::Error>
map_data_to_plot(const AxesSpec &axes, double data_x, double data_y);

[[nodiscard]] std::expected<PlotPoint, diagnostics::Error>
map_plot_to_data(const AxesSpec &axes, float plot_x, float plot_y);

// Build ticks, labels, optional grid, and DrawOps. Label collisions never fail
// the layout — they append AxesWarning entries instead.
[[nodiscard]] std::expected<AxesLayout, diagnostics::Error>
layout_axes(const AxesSpec &spec);

} // namespace instantee::render_core
