#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/plot_axes.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace instantee::render_core {

// Safe, bounded scalar expression for y = f(x). No strings, no eval, no
// callbacks — only typed nodes with depth/sample budgets.
enum class PlotExprOp : std::uint8_t {
  constant = 1,
  variable_x,
  add,
  sub,
  mul,
  div,
  neg,
  sin,
  cos,
  abs,
  // Integer power only (exponent in PlotExprNode::constant).
  pow_i,
};

struct PlotExprNode {
  PlotExprOp op = PlotExprOp::constant;
  double constant = 0.0;   // value for constant / integer exponent for pow_i
  std::uint32_t left = 0;  // child index (unary/binary)
  std::uint32_t right = 0; // child index (binary)
};

struct PlotExpr {
  std::vector<PlotExprNode> nodes{}; // root is the last node when non-empty
};

struct PlotCurveLimits {
  std::uint32_t max_ast_nodes = 64;
  std::uint32_t max_ast_depth = 12;
  std::uint32_t min_samples = 32;
  std::uint32_t max_samples = 1024;
  // Subdivide a segment when the mid-point chord error exceeds this (pixels).
  float adaptive_tol_px = 0.75f;
  // Break the polyline when consecutive plot points jump farther than this.
  float discontinuity_jump_px = 80.0f;
  // Reject |pow| exponents above this (integer powers only).
  std::int32_t max_pow_exp = 8;
};

struct PlotCurveSpec {
  AxesSpec axes{};
  PlotExpr expr{};
  float stroke_width = 2.0f;
  Rgba8 stroke_color{20, 40, 90, 255};
  PlotCurveLimits limits{};
};

struct PlotCurveSegment {
  std::vector<PlotPoint> points{}; // already clipped to the plot rect
};

struct PlotCurveSample {
  std::vector<PlotCurveSegment> segments{};
  std::vector<DrawOp> ops{};
  std::uint32_t sample_evaluations = 0;
};

// Builders — each returns an expression or an error when budgets would break.
[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_constant(double value, const PlotCurveLimits &limits = {});

[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_variable_x(const PlotCurveLimits &limits = {});

[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_unary(PlotExprOp op, PlotExpr inner,
                const PlotCurveLimits &limits = {});

[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_binary(PlotExprOp op, PlotExpr left, PlotExpr right,
                 const PlotCurveLimits &limits = {});

[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_pow_i(PlotExpr base, std::int32_t exponent,
                const PlotCurveLimits &limits = {});

// Polynomial c0 + c1 x + c2 x^2 + ... (Horner form in the AST).
[[nodiscard]] std::expected<PlotExpr, diagnostics::Error>
make_plot_polynomial(std::span<const double> coeffs_low_to_high,
                     const PlotCurveLimits &limits = {});

// Evaluate y = f(x). Non-finite results are reported as nullopt via expected
// error code plot_curve.undefined (callers treat as a segment break).
[[nodiscard]] std::expected<double, diagnostics::Error>
eval_plot_expr(const PlotExpr &expr, double x);

// Adaptive sample + clip into the axes plot rectangle. Splits segments on
// undefined values and large jumps (discontinuities).
[[nodiscard]] std::expected<PlotCurveSample, diagnostics::Error>
sample_plot_curve(const PlotCurveSpec &spec);

} // namespace instantee::render_core
