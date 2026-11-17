#include <instantee/runtime/easing.hpp>
#include <instantee/runtime/timeline.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_near(double actual, double expected, double tol,
                 const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= tol)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;

  // Reject out-of-range bezier controls.
  expect(!make_cubic_bezier_controls(-0.1, 0.0, 1.0, 1.0).has_value(),
         "reject x1 < 0");
  expect(!make_cubic_bezier_controls(0.0, 0.0, 1.1, 1.0).has_value(),
         "reject x2 > 1");

  auto linear_bez = make_cubic_bezier_controls(0.0, 0.0, 1.0, 1.0);
  expect(linear_bez.has_value(), "identity bezier controls");
  if (linear_bez) {
    expect_near(sample_cubic_bezier(*linear_bez, 0.0), 0.0, 0.0,
                "bezier u=0 exact");
    expect_near(sample_cubic_bezier(*linear_bez, 1.0), 1.0, 0.0,
                "bezier u=1 exact");
    expect_near(sample_cubic_bezier(*linear_bez, 0.5), 0.5, 1e-9,
                "identity bezier mid");
    expect_near(sample_cubic_bezier(*linear_bez, -0.25), 0.0, 0.0,
                "unit time clamped low");
    expect_near(sample_cubic_bezier(*linear_bez, 1.5), 1.0, 0.0,
                "unit time clamped high");
  }

  // ease-in-out style: slower near ends, continuous and finite on the grid.
  auto ease = make_cubic_bezier_controls(0.42, 0.0, 0.58, 1.0);
  expect(ease.has_value(), "ease-in-out controls");
  if (ease) {
    double prev = sample_cubic_bezier(*ease, 0.0);
    expect_near(prev, 0.0, 0.0, "ease start");
    for (int i = 1; i <= 20; ++i) {
      const double u = static_cast<double>(i) / 20.0;
      const double y = sample_cubic_bezier(*ease, u);
      expect(std::isfinite(y), "ease sample finite");
      expect(y + 1e-12 >= prev, "ease non-decreasing for this curve");
      prev = y;
    }
    expect_near(sample_cubic_bezier(*ease, 1.0), 1.0, 0.0, "ease end");
  }

  // Track-level step / linear / bezier continuity and boundaries.
  auto step = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{10}, 10.0},
      },
      TrackInterpolation::hold);
  expect(step.has_value(), "step track");
  if (step) {
    expect_near(*evaluate_scalar_track(*step, FrameIndex{0}), 0.0, 1e-12,
                "step at left");
    expect_near(*evaluate_scalar_track(*step, FrameIndex{9}), 0.0, 1e-12,
                "step end-exclusive interior");
    expect_near(*evaluate_scalar_track(*step, FrameIndex{10}), 10.0, 1e-12,
                "step at last keyframe");
  }

  auto linear = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{10}, 10.0},
      },
      TrackInterpolation::linear);
  expect(linear.has_value(), "linear track");
  if (linear) {
    expect_near(*evaluate_scalar_track(*linear, FrameIndex{0}), 0.0, 1e-12,
                "linear start");
    expect_near(*evaluate_scalar_track(*linear, FrameIndex{5}), 5.0, 1e-12,
                "linear mid");
    expect_near(*evaluate_scalar_track(*linear, FrameIndex{10}), 10.0, 1e-12,
                "linear last keyframe");
  }

  auto bez_ctrl = make_cubic_bezier_controls(0.0, 0.0, 1.0, 1.0);
  auto bezier = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 2.0},
          ScalarKeyframe{FrameIndex{8}, 6.0},
      },
      TrackInterpolation::cubic_bezier, TrackEndBehavior::clamp, *bez_ctrl);
  expect(bezier.has_value(), "bezier track");
  if (bezier) {
    expect_near(*evaluate_scalar_track(*bezier, FrameIndex{0}), 2.0, 1e-12,
                "bezier meets left keyframe");
    expect_near(*evaluate_scalar_track(*bezier, FrameIndex{8}), 6.0, 1e-12,
                "bezier meets right/last keyframe");
    expect_near(*evaluate_scalar_track(*bezier, FrameIndex{4}), 4.0, 1e-9,
                "identity bezier mid value");
    expect(std::isfinite(*evaluate_scalar_track(*bezier, FrameIndex{1})),
           "bezier interior finite");
  }

  // Documented extrapolation: linear slope beyond the span; clamp otherwise.
  auto extrap = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{10}, 10.0},
          ScalarKeyframe{FrameIndex{20}, 20.0},
      },
      TrackInterpolation::linear, TrackEndBehavior::extrapolate_linear);
  expect(extrap.has_value(), "extrapolating linear track");
  if (extrap) {
    expect_near(*evaluate_scalar_track(*extrap, FrameIndex{0}), 0.0, 1e-12,
                "extrapolate before first");
    expect_near(*evaluate_scalar_track(*extrap, FrameIndex{30}), 30.0, 1e-12,
                "extrapolate after last");
  }

  auto clamped = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{10}, 10.0},
          ScalarKeyframe{FrameIndex{20}, 20.0},
      },
      TrackInterpolation::linear, TrackEndBehavior::clamp);
  if (clamped) {
    expect_near(*evaluate_scalar_track(*clamped, FrameIndex{0}), 10.0, 1e-12,
                "clamp before first");
    expect_near(*evaluate_scalar_track(*clamped, FrameIndex{30}), 20.0, 1e-12,
                "clamp after last");
  }

  // cubic_bezier without controls fails at construction.
  auto missing = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{1}, 1.0},
      },
      TrackInterpolation::cubic_bezier);
  expect(!missing.has_value(), "bezier requires controls");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
