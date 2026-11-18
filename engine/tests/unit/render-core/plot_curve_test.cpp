#include <instantee/render_core/plot_curve.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

using namespace instantee::render_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool near(double a, double b, double eps = 1e-6) {
  return std::fabs(a - b) <= eps;
}

[[nodiscard]] AxesSpec unit_axes() {
  AxesSpec a;
  a.plot = Rect2f{.x = 0, .y = 0, .width = 200, .height = 100};
  a.x = AxisDomain{.min = -3.141592653589793, .max = 3.141592653589793};
  a.y = AxisDomain{.min = -1.5, .max = 1.5};
  a.show_grid = false;
  return a;
}

} // namespace

int main() {
  PlotCurveLimits limits;
  limits.min_samples = 48;
  limits.max_samples = 256;
  limits.adaptive_tol_px = 0.5f;
  limits.discontinuity_jump_px = 40.0f;

  // Polynomial y = 1 + 2x + x^2 at x=3 → 16.
  {
    const double coeffs[] = {1.0, 2.0, 1.0};
    auto poly = make_plot_polynomial(coeffs, limits);
    expect(poly.has_value(), "make polynomial");
    if (poly) {
      auto y = eval_plot_expr(*poly, 3.0);
      expect(y.has_value() && near(*y, 16.0), "poly eval");
    }
  }

  // Sine curve stays within plot and produces a continuous segment.
  {
    auto x = make_plot_variable_x(limits);
    auto sine = make_plot_unary(PlotExprOp::sin, *x, limits);
    expect(sine.has_value(), "make sin(x)");
    PlotCurveSpec spec;
    spec.axes = unit_axes();
    spec.expr = *sine;
    spec.limits = limits;
    auto sample = sample_plot_curve(spec);
    expect(sample.has_value(), "sample sine");
    if (sample) {
      expect(sample->sample_evaluations <= limits.max_samples * 4,
             "eval budget");
      expect(!sample->segments.empty(), "has segments");
      expect(!sample->ops.empty(), "has stroke ops");
      std::size_t pts = 0;
      for (const auto &seg : sample->segments) {
        pts += seg.points.size();
        for (const auto &p : seg.points) {
          expect(p.x >= spec.axes.plot.x - 1e-3f &&
                     p.x <= spec.axes.plot.x + spec.axes.plot.width + 1e-3f &&
                     p.y >= spec.axes.plot.y - 1e-3f &&
                     p.y <= spec.axes.plot.y + spec.axes.plot.height + 1e-3f,
                 "clipped to plot");
        }
      }
      expect(pts >= 16, "enough samples");
      // sin(0)=0 maps to vertical mid of plot.
      auto y0 = eval_plot_expr(*sine, 0.0);
      expect(y0 && near(*y0, 0.0), "sin(0)");
    }
  }

  // 1/x: discontinuity at 0 → multiple segments, no infinite points.
  {
    auto x = make_plot_variable_x(limits);
    auto one = make_plot_constant(1.0, limits);
    auto inv = make_plot_binary(PlotExprOp::div, *one, *x, limits);
    expect(inv.has_value(), "make 1/x");
    PlotCurveSpec spec;
    spec.axes.plot = Rect2f{.x = 10, .y = 10, .width = 180, .height = 120};
    spec.axes.x = AxisDomain{.min = -2.0, .max = 2.0};
    spec.axes.y = AxisDomain{.min = -5.0, .max = 5.0};
    spec.axes.show_grid = false;
    spec.expr = *inv;
    spec.limits = limits;
    spec.limits.discontinuity_jump_px = 30.0f;
    auto sample = sample_plot_curve(spec);
    expect(sample.has_value(), "sample 1/x");
    if (sample) {
      expect(sample->segments.size() >= 2, "split across discontinuity");
      for (const auto &seg : sample->segments) {
        expect(seg.points.size() >= 2, "segment has edge");
        for (const auto &p : seg.points) {
          expect(std::isfinite(p.x) && std::isfinite(p.y), "finite pixels");
        }
      }
    }
  }

  // Reject oversized power and empty expr.
  {
    auto x = make_plot_variable_x(limits);
    expect(!make_plot_pow_i(*x, 100, limits).has_value(), "reject huge pow");
    PlotCurveSpec empty;
    empty.axes = unit_axes();
    expect(!sample_plot_curve(empty).has_value(), "reject empty expr");
  }

  // Domain change does not require rebuilding the expression AST.
  {
    const double coeffs[] = {0.0, 1.0}; // y = x
    auto line = make_plot_polynomial(coeffs, limits);
    PlotCurveSpec spec;
    spec.axes = unit_axes();
    spec.axes.x = AxisDomain{.min = 0, .max = 1};
    spec.axes.y = AxisDomain{.min = 0, .max = 1};
    spec.expr = *line;
    spec.limits = limits;
    auto a = sample_plot_curve(spec);
    spec.axes.x.max = 2;
    auto b = sample_plot_curve(spec);
    expect(a && b, "resample after domain change");
    if (a && b) {
      expect(!a->ops.empty() && !b->ops.empty(), "both produce strokes");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "plot_curve_test: ok\n";
  return EXIT_SUCCESS;
}
