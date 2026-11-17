#include <instantee/runtime/easing.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

bool in_unit_interval(double value) noexcept {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

double bezier_coord(double t, double p1, double p2) noexcept {
  // Bernstein form with P0=0, P3=1.
  const double u = 1.0 - t;
  return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
}

double bezier_coord_derivative(double t, double p1, double p2) noexcept {
  const double u = 1.0 - t;
  return 3.0 * u * u * p1 + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (1.0 - p2);
}

// Invert Bx(t) = unit_time for t ∈ [0, 1]. Newton with bisection fallback;
// fixed iteration caps keep the result deterministic across call orders.
double solve_bezier_time(double unit_time, double x1, double x2) noexcept {
  if (unit_time <= 0.0) {
    return 0.0;
  }
  if (unit_time >= 1.0) {
    return 1.0;
  }

  double t = unit_time;
  double lo = 0.0;
  double hi = 1.0;
  constexpr int kMaxIterations = 12;
  constexpr double kEpsilon = 1e-9;
  constexpr double kMinSlope = 1e-7;

  for (int i = 0; i < kMaxIterations; ++i) {
    const double x = bezier_coord(t, x1, x2);
    const double delta = x - unit_time;
    if (std::abs(delta) <= kEpsilon) {
      return t;
    }
    if (delta > 0.0) {
      hi = t;
    } else {
      lo = t;
    }
    const double slope = bezier_coord_derivative(t, x1, x2);
    if (std::abs(slope) >= kMinSlope) {
      const double next = t - delta / slope;
      if (next > lo && next < hi) {
        t = next;
        continue;
      }
    }
    t = 0.5 * (lo + hi);
  }
  return t;
}

} // namespace

std::expected<CubicBezierControls, diagnostics::Error>
make_cubic_bezier_controls(double x1, double y1, double x2, double y2) {
  if (!in_unit_interval(x1) || !in_unit_interval(y1) || !in_unit_interval(x2) ||
      !in_unit_interval(y2)) {
    return std::unexpected(
        err("invalid_bezier_controls",
            "Cubic-bezier control points must be finite and inside [0, 1]"));
  }
  return CubicBezierControls{.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2};
}

double sample_cubic_bezier(const CubicBezierControls &controls,
                           double unit_time) noexcept {
  const double u = std::clamp(unit_time, 0.0, 1.0);
  if (u <= 0.0) {
    return 0.0;
  }
  if (u >= 1.0) {
    return 1.0;
  }
  const double t = solve_bezier_time(u, controls.x1, controls.x2);
  return bezier_coord(t, controls.y1, controls.y2);
}

} // namespace instantee::runtime
