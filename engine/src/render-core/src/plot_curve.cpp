#include <instantee/render_core/plot_curve.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "plot_curve", message};
}

[[nodiscard]] bool is_unary(PlotExprOp op) noexcept {
  return op == PlotExprOp::neg || op == PlotExprOp::sin ||
         op == PlotExprOp::cos || op == PlotExprOp::abs ||
         op == PlotExprOp::pow_i;
}

[[nodiscard]] bool is_binary(PlotExprOp op) noexcept {
  return op == PlotExprOp::add || op == PlotExprOp::sub ||
         op == PlotExprOp::mul || op == PlotExprOp::div;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_limits(const PlotCurveLimits &limits) {
  if (limits.max_ast_nodes == 0 || limits.max_ast_depth == 0) {
    return std::unexpected(
        make_invalid("plot_curve.bad_limits", "AST budgets must be > 0"));
  }
  if (limits.min_samples == 0 || limits.max_samples == 0 ||
      limits.min_samples > limits.max_samples) {
    return std::unexpected(
        make_invalid("plot_curve.bad_limits",
                     "sample budgets require 0 < min_samples <= max_samples"));
  }
  if (!std::isfinite(limits.adaptive_tol_px) ||
      !(limits.adaptive_tol_px > 0.0f) ||
      !std::isfinite(limits.discontinuity_jump_px) ||
      !(limits.discontinuity_jump_px > 0.0f)) {
    return std::unexpected(make_invalid(
        "plot_curve.bad_limits",
        "adaptive_tol_px and discontinuity_jump_px must be finite and > 0"));
  }
  if (limits.max_pow_exp < 0) {
    return std::unexpected(
        make_invalid("plot_curve.bad_limits", "max_pow_exp must be >= 0"));
  }
  return {};
}

[[nodiscard]] std::uint32_t ast_depth(const PlotExpr &expr, std::uint32_t idx,
                                      std::uint32_t budget,
                                      diagnostics::Error *err) {
  if (!err->code.empty()) {
    return 0;
  }
  if (idx >= expr.nodes.size()) {
    *err = make_invalid("plot_curve.bad_ast", "expression child index OOB");
    return 0;
  }
  if (budget == 0) {
    *err = make_invalid("plot_curve.depth", "expression exceeds max_ast_depth");
    return 0;
  }
  const PlotExprNode &n = expr.nodes[idx];
  switch (n.op) {
  case PlotExprOp::constant:
  case PlotExprOp::variable_x:
    return 1;
  case PlotExprOp::neg:
  case PlotExprOp::sin:
  case PlotExprOp::cos:
  case PlotExprOp::abs:
  case PlotExprOp::pow_i: {
    const std::uint32_t d = ast_depth(expr, n.left, budget - 1, err);
    return err->code.empty() ? d + 1 : 0;
  }
  case PlotExprOp::add:
  case PlotExprOp::sub:
  case PlotExprOp::mul:
  case PlotExprOp::div: {
    const std::uint32_t dl = ast_depth(expr, n.left, budget - 1, err);
    const std::uint32_t dr = ast_depth(expr, n.right, budget - 1, err);
    return err->code.empty() ? std::max(dl, dr) + 1 : 0;
  }
  }
  *err = make_invalid("plot_curve.bad_ast", "unknown expression op");
  return 0;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_expr(const PlotExpr &expr, const PlotCurveLimits &limits) {
  if (expr.nodes.empty()) {
    return std::unexpected(
        make_invalid("plot_curve.empty", "plot expression is empty"));
  }
  if (expr.nodes.size() > limits.max_ast_nodes) {
    return std::unexpected(
        make_invalid("plot_curve.nodes", "expression exceeds max_ast_nodes"));
  }
  diagnostics::Error err{};
  (void)ast_depth(expr, static_cast<std::uint32_t>(expr.nodes.size() - 1),
                  limits.max_ast_depth, &err);
  if (!err.code.empty()) {
    return std::unexpected(err);
  }
  return {};
}

[[nodiscard]] std::expected<double, diagnostics::Error>
eval_node(const PlotExpr &expr, std::uint32_t idx, double x,
          std::uint32_t depth_left) {
  if (idx >= expr.nodes.size()) {
    return std::unexpected(
        make_invalid("plot_curve.bad_ast", "expression child index OOB"));
  }
  if (depth_left == 0) {
    return std::unexpected(
        make_invalid("plot_curve.depth", "expression exceeds max_ast_depth"));
  }
  const PlotExprNode &n = expr.nodes[idx];
  switch (n.op) {
  case PlotExprOp::constant:
    if (!std::isfinite(n.constant)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "non-finite constant"));
    }
    return n.constant;
  case PlotExprOp::variable_x:
    if (!std::isfinite(x)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "non-finite x"));
    }
    return x;
  case PlotExprOp::neg: {
    auto v = eval_node(expr, n.left, x, depth_left - 1);
    if (!v) {
      return v;
    }
    return -*v;
  }
  case PlotExprOp::sin: {
    auto v = eval_node(expr, n.left, x, depth_left - 1);
    if (!v) {
      return v;
    }
    const double y = std::sin(*v);
    if (!std::isfinite(y)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "sin undefined"));
    }
    return y;
  }
  case PlotExprOp::cos: {
    auto v = eval_node(expr, n.left, x, depth_left - 1);
    if (!v) {
      return v;
    }
    const double y = std::cos(*v);
    if (!std::isfinite(y)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "cos undefined"));
    }
    return y;
  }
  case PlotExprOp::abs: {
    auto v = eval_node(expr, n.left, x, depth_left - 1);
    if (!v) {
      return v;
    }
    return std::fabs(*v);
  }
  case PlotExprOp::pow_i: {
    auto base = eval_node(expr, n.left, x, depth_left - 1);
    if (!base) {
      return base;
    }
    const double expv = n.constant;
    if (!std::isfinite(expv) || expv != std::rint(expv)) {
      return std::unexpected(make_invalid("plot_curve.bad_pow",
                                          "pow_i requires integer exponent"));
    }
    const auto e = static_cast<int>(expv);
    const double y = std::pow(*base, e);
    if (!std::isfinite(y)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "pow undefined"));
    }
    return y;
  }
  case PlotExprOp::add:
  case PlotExprOp::sub:
  case PlotExprOp::mul:
  case PlotExprOp::div: {
    auto l = eval_node(expr, n.left, x, depth_left - 1);
    if (!l) {
      return l;
    }
    auto r = eval_node(expr, n.right, x, depth_left - 1);
    if (!r) {
      return r;
    }
    double y = 0.0;
    switch (n.op) {
    case PlotExprOp::add:
      y = *l + *r;
      break;
    case PlotExprOp::sub:
      y = *l - *r;
      break;
    case PlotExprOp::mul:
      y = *l * *r;
      break;
    case PlotExprOp::div:
      if (*r == 0.0) {
        return std::unexpected(
            make_invalid("plot_curve.undefined", "division by zero"));
      }
      y = *l / *r;
      break;
    default:
      break;
    }
    if (!std::isfinite(y)) {
      return std::unexpected(
          make_invalid("plot_curve.undefined", "binary op undefined"));
    }
    return y;
  }
  }
  return std::unexpected(
      make_invalid("plot_curve.bad_ast", "unknown expression op"));
}

[[nodiscard]] PlotExpr merge_expr(PlotExpr a, PlotExpr b) {
  const auto offset = static_cast<std::uint32_t>(a.nodes.size());
  for (PlotExprNode &n : b.nodes) {
    if (n.op != PlotExprOp::constant && n.op != PlotExprOp::variable_x) {
      n.left += offset;
      if (is_binary(n.op)) {
        n.right += offset;
      }
    }
    a.nodes.push_back(n);
  }
  return a;
}

[[nodiscard]] DrawOp make_line(float x0, float y0, float x1, float y1,
                               float width, Rgba8 color) {
  return DrawOp{.kind = DrawOpKind::stroke_line,
                .rect = Rect2f{.x = x0, .y = y0, .width = 1, .height = 1},
                .x1 = x1,
                .y1 = y1,
                .color = color,
                .stroke_width = width};
}

[[nodiscard]] float dist_px(PlotPoint a, PlotPoint b) noexcept {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] bool inside_plot(const Rect2f &plot, PlotPoint p) noexcept {
  return p.x >= plot.x && p.x <= plot.x + plot.width && p.y >= plot.y &&
         p.y <= plot.y + plot.height;
}

// Cohen–Sutherland style clip of a segment to the plot AABB (inclusive).
[[nodiscard]] bool clip_segment(const Rect2f &plot, PlotPoint a, PlotPoint b,
                                PlotPoint *out_a, PlotPoint *out_b) {
  const float xmin = plot.x;
  const float xmax = plot.x + plot.width;
  const float ymin = plot.y;
  const float ymax = plot.y + plot.height;

  auto code = [&](PlotPoint p) {
    int c = 0;
    if (p.x < xmin) {
      c |= 1;
    } else if (p.x > xmax) {
      c |= 2;
    }
    if (p.y < ymin) {
      c |= 4;
    } else if (p.y > ymax) {
      c |= 8;
    }
    return c;
  };

  int ca = code(a);
  int cb = code(b);
  for (;;) {
    if (!(ca | cb)) {
      *out_a = a;
      *out_b = b;
      return true;
    }
    if (ca & cb) {
      return false;
    }
    const int out = ca ? ca : cb;
    PlotPoint p{};
    if (out & 8) { // above
      p.x = a.x + (b.x - a.x) * (ymax - a.y) / (b.y - a.y);
      p.y = ymax;
    } else if (out & 4) { // below
      p.x = a.x + (b.x - a.x) * (ymin - a.y) / (b.y - a.y);
      p.y = ymin;
    } else if (out & 2) { // right
      p.y = a.y + (b.y - a.y) * (xmax - a.x) / (b.x - a.x);
      p.x = xmax;
    } else { // left
      p.y = a.y + (b.y - a.y) * (xmin - a.x) / (b.x - a.x);
      p.x = xmin;
    }
    if (out == ca) {
      a = p;
      ca = code(a);
    } else {
      b = p;
      cb = code(b);
    }
  }
}

struct SamplePoint {
  double x = 0;
  double y = 0;
  bool ok = false;
  PlotPoint px{};
};

} // namespace

std::expected<PlotExpr, diagnostics::Error>
make_plot_constant(double value, const PlotCurveLimits &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(value)) {
    return std::unexpected(
        make_invalid("plot_curve.bad_const", "constant must be finite"));
  }
  PlotExpr expr;
  expr.nodes.push_back(
      PlotExprNode{.op = PlotExprOp::constant, .constant = value});
  return expr;
}

std::expected<PlotExpr, diagnostics::Error>
make_plot_variable_x(const PlotCurveLimits &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  PlotExpr expr;
  expr.nodes.push_back(PlotExprNode{.op = PlotExprOp::variable_x});
  return expr;
}

std::expected<PlotExpr, diagnostics::Error>
make_plot_unary(PlotExprOp op, PlotExpr inner, const PlotCurveLimits &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (!is_unary(op) || op == PlotExprOp::pow_i) {
    return std::unexpected(make_invalid(
        "plot_curve.bad_op", "expected unary op (use pow_i helper)"));
  }
  if (auto ok = validate_expr(inner, limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (inner.nodes.size() + 1 > limits.max_ast_nodes) {
    return std::unexpected(
        make_invalid("plot_curve.nodes", "expression exceeds max_ast_nodes"));
  }
  const auto left = static_cast<std::uint32_t>(inner.nodes.size() - 1);
  inner.nodes.push_back(PlotExprNode{.op = op, .left = left});
  if (auto ok = validate_expr(inner, limits); !ok) {
    return std::unexpected(ok.error());
  }
  return inner;
}

std::expected<PlotExpr, diagnostics::Error>
make_plot_binary(PlotExprOp op, PlotExpr left, PlotExpr right,
                 const PlotCurveLimits &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (!is_binary(op)) {
    return std::unexpected(
        make_invalid("plot_curve.bad_op", "expected binary op"));
  }
  if (auto ok = validate_expr(left, limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_expr(right, limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (left.nodes.size() + right.nodes.size() + 1 > limits.max_ast_nodes) {
    return std::unexpected(
        make_invalid("plot_curve.nodes", "expression exceeds max_ast_nodes"));
  }
  const auto lroot = static_cast<std::uint32_t>(left.nodes.size() - 1);
  PlotExpr merged = merge_expr(std::move(left), std::move(right));
  const auto rroot = static_cast<std::uint32_t>(merged.nodes.size() - 1);
  merged.nodes.push_back(PlotExprNode{.op = op, .left = lroot, .right = rroot});
  if (auto ok = validate_expr(merged, limits); !ok) {
    return std::unexpected(ok.error());
  }
  return merged;
}

std::expected<PlotExpr, diagnostics::Error>
make_plot_pow_i(PlotExpr base, std::int32_t exponent,
                const PlotCurveLimits &limits) {
  if (auto ok = validate_limits(limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (std::abs(exponent) > limits.max_pow_exp) {
    return std::unexpected(
        make_invalid("plot_curve.bad_pow", "pow exponent exceeds max_pow_exp"));
  }
  if (auto ok = validate_expr(base, limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (base.nodes.size() + 1 > limits.max_ast_nodes) {
    return std::unexpected(
        make_invalid("plot_curve.nodes", "expression exceeds max_ast_nodes"));
  }
  const auto left = static_cast<std::uint32_t>(base.nodes.size() - 1);
  base.nodes.push_back(PlotExprNode{.op = PlotExprOp::pow_i,
                                    .constant = static_cast<double>(exponent),
                                    .left = left});
  if (auto ok = validate_expr(base, limits); !ok) {
    return std::unexpected(ok.error());
  }
  return base;
}

std::expected<PlotExpr, diagnostics::Error>
make_plot_polynomial(std::span<const double> coeffs_low_to_high,
                     const PlotCurveLimits &limits) {
  if (coeffs_low_to_high.empty()) {
    return std::unexpected(
        make_invalid("plot_curve.empty", "polynomial needs at least c0"));
  }
  for (double c : coeffs_low_to_high) {
    if (!std::isfinite(c)) {
      return std::unexpected(
          make_invalid("plot_curve.bad_const", "coeff must be finite"));
    }
  }
  // Horner: ((cn x + c[n-1]) x + ... ) x + c0
  auto x = make_plot_variable_x(limits);
  if (!x) {
    return x;
  }
  auto acc = make_plot_constant(coeffs_low_to_high.back(), limits);
  if (!acc) {
    return acc;
  }
  for (std::size_t i = coeffs_low_to_high.size() - 1; i-- > 0;) {
    auto mul = make_plot_binary(PlotExprOp::mul, *acc, *x, limits);
    if (!mul) {
      return mul;
    }
    auto c = make_plot_constant(coeffs_low_to_high[i], limits);
    if (!c) {
      return c;
    }
    acc = make_plot_binary(PlotExprOp::add, *mul, *c, limits);
    if (!acc) {
      return acc;
    }
  }
  return acc;
}

std::expected<double, diagnostics::Error> eval_plot_expr(const PlotExpr &expr,
                                                         double x) {
  if (expr.nodes.empty()) {
    return std::unexpected(
        make_invalid("plot_curve.empty", "plot expression is empty"));
  }
  return eval_node(expr, static_cast<std::uint32_t>(expr.nodes.size() - 1), x,
                   64);
}

std::expected<PlotCurveSample, diagnostics::Error>
sample_plot_curve(const PlotCurveSpec &spec) {
  if (auto ok = validate_limits(spec.limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_expr(spec.expr, spec.limits); !ok) {
    return std::unexpected(ok.error());
  }
  if (!std::isfinite(spec.stroke_width) || !(spec.stroke_width > 0.0f)) {
    return std::unexpected(make_invalid("plot_curve.bad_stroke",
                                        "stroke_width must be finite and > 0"));
  }

  const double xmin = spec.axes.x.min;
  const double xmax = spec.axes.x.max;
  if (!(xmax > xmin)) {
    return std::unexpected(
        make_invalid("plot_curve.bad_domain", "x domain max must be > min"));
  }

  std::uint32_t evals = 0;
  auto evaluate = [&](double x) -> SamplePoint {
    SamplePoint s;
    s.x = x;
    ++evals;
    if (evals > spec.limits.max_samples * 4) {
      s.ok = false;
      return s;
    }
    auto y = eval_node(spec.expr,
                       static_cast<std::uint32_t>(spec.expr.nodes.size() - 1),
                       x, spec.limits.max_ast_depth + 1);
    if (!y) {
      s.ok = false;
      return s;
    }
    s.y = *y;
    auto px = map_data_to_plot(spec.axes, x, *y);
    if (!px) {
      s.ok = false;
      return s;
    }
    s.px = *px;
    s.ok = true;
    return s;
  };

  // Seed uniform samples, then adaptively refine.
  std::vector<SamplePoint> samples;
  samples.reserve(spec.limits.max_samples);
  const std::uint32_t seed_n = spec.limits.min_samples;
  for (std::uint32_t i = 0; i < seed_n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(seed_n - 1);
    samples.push_back(evaluate(xmin + t * (xmax - xmin)));
  }

  for (std::size_t i = 0; i + 1 < samples.size();) {
    if (samples.size() >= spec.limits.max_samples) {
      break;
    }
    const SamplePoint &a = samples[i];
    const SamplePoint &b = samples[i + 1];
    if (!a.ok || !b.ok) {
      ++i;
      continue;
    }
    const double xm = 0.5 * (a.x + b.x);
    SamplePoint mid = evaluate(xm);
    if (!mid.ok) {
      ++i;
      continue;
    }
    // Chord error: distance from mid pixel to segment a-b.
    const float dx = b.px.x - a.px.x;
    const float dy = b.px.y - a.px.y;
    const float len2 = dx * dx + dy * dy;
    float err = 0.0f;
    if (len2 > 1e-12f) {
      const float t =
          ((mid.px.x - a.px.x) * dx + (mid.px.y - a.px.y) * dy) / len2;
      const float cx = a.px.x + t * dx;
      const float cy = a.px.y + t * dy;
      err = dist_px(mid.px, PlotPoint{cx, cy});
    } else {
      err = dist_px(mid.px, a.px);
    }
    if (err > spec.limits.adaptive_tol_px &&
        samples.size() < spec.limits.max_samples) {
      samples.insert(samples.begin() + static_cast<std::ptrdiff_t>(i + 1), mid);
      // Re-check the same index with the new mid.
      continue;
    }
    ++i;
  }

  PlotCurveSample out;
  out.sample_evaluations = evals;
  PlotCurveSegment current;
  auto flush = [&]() {
    if (current.points.size() >= 2) {
      out.segments.push_back(std::move(current));
    }
    current = PlotCurveSegment{};
  };

  std::optional<SamplePoint> prev;
  for (const SamplePoint &s : samples) {
    if (!s.ok) {
      flush();
      prev.reset();
      continue;
    }
    if (prev && prev->ok) {
      const float jump = dist_px(prev->px, s.px);
      if (jump > spec.limits.discontinuity_jump_px) {
        flush();
        prev = s;
        // Start new segment at this point if inside (or clipped later).
        if (inside_plot(spec.axes.plot, s.px)) {
          current.points.push_back(s.px);
        }
        continue;
      }
      PlotPoint ca{};
      PlotPoint cb{};
      if (clip_segment(spec.axes.plot, prev->px, s.px, &ca, &cb)) {
        if (current.points.empty()) {
          current.points.push_back(ca);
        } else if (dist_px(current.points.back(), ca) > 1e-3f) {
          // Gap after clip — start fresh.
          flush();
          current.points.push_back(ca);
        }
        if (dist_px(current.points.back(), cb) > 1e-4f) {
          current.points.push_back(cb);
        }
      } else {
        flush();
      }
    } else if (inside_plot(spec.axes.plot, s.px)) {
      current.points.push_back(s.px);
    }
    prev = s;
  }
  flush();

  for (const auto &seg : out.segments) {
    for (std::size_t i = 1; i < seg.points.size(); ++i) {
      const auto &a = seg.points[i - 1];
      const auto &b = seg.points[i];
      out.ops.push_back(
          make_line(a.x, a.y, b.x, b.y, spec.stroke_width, spec.stroke_color));
    }
  }
  return out;
}

} // namespace instantee::render_core
