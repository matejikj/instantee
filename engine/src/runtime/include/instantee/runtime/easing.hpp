#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>

namespace instantee::runtime {

// CSS-style cubic-bezier control points for unit easing. Schema requires all
// four components in [0, 1]; x1/x2 in that range keep Bx(t) strictly usable
// for stable time-axis inversion.
struct CubicBezierControls {
  double x1 = 0.0;
  double y1 = 0.0;
  double x2 = 1.0;
  double y2 = 1.0;
};

[[nodiscard]] std::expected<CubicBezierControls, diagnostics::Error>
make_cubic_bezier_controls(double x1, double y1, double x2, double y2);

// Maps unit time progress u ∈ [0, 1] to unit eased progress. Endpoints are
// exact (0→0, 1→1). Input u is clamped to [0, 1] before evaluation so callers
// never observe NaN from out-of-range time.
[[nodiscard]] double sample_cubic_bezier(const CubicBezierControls &controls,
                                         double unit_time) noexcept;

} // namespace instantee::runtime
