#include <instantee/runtime/timeline.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_near(double actual, double expected, const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= 1e-12)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;

  // Shared film-absolute track spanning two shot intervals [0,30) and [30,60).
  auto track = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{0}, 0.0},
          ScalarKeyframe{FrameIndex{20}, 1.0},
          ScalarKeyframe{FrameIndex{40}, 0.5},
      },
      TrackInterpolation::linear);
  expect(track.has_value(), "make linear track");
  if (!track) {
    return EXIT_FAILURE;
  }

  // Random-access order: mid → start → end → across shot boundary.
  const auto mid = evaluate_scalar_track(*track, FrameIndex{10});
  const auto start = evaluate_scalar_track(*track, FrameIndex{0});
  const auto end = evaluate_scalar_track(*track, FrameIndex{40});
  const auto across = evaluate_scalar_track(*track, FrameIndex{30});
  expect(mid.has_value() && start.has_value() && end.has_value() &&
             across.has_value(),
         "random-access samples succeed");
  if (mid && start && end && across) {
    expect_near(*start, 0.0, "value at first keyframe");
    expect_near(*end, 0.5, "value at last keyframe");
    expect_near(*mid, 0.5, "linear mid-segment value");
    // Frame 30 is inside [20,40) after the shot cut at 30.
    expect_near(*across, 0.75, "shared track across shot boundary");
  }

  // Hold keeps the left keyframe inside the half-open segment.
  auto hold = make_scalar_track(
      {
          ScalarKeyframe{FrameIndex{5}, 2.0},
          ScalarKeyframe{FrameIndex{15}, 8.0},
      },
      TrackInterpolation::hold);
  expect(hold.has_value(), "make hold track");
  if (hold) {
    expect_near(*evaluate_scalar_track(*hold, FrameIndex{5}), 2.0,
                "hold at segment start");
    expect_near(*evaluate_scalar_track(*hold, FrameIndex{14}), 2.0,
                "hold within interval");
    expect_near(*evaluate_scalar_track(*hold, FrameIndex{15}), 8.0,
                "hold at next keyframe");
  }

  // Clamp end behavior before first / after last.
  if (hold) {
    expect_near(*evaluate_scalar_track(*hold, FrameIndex{0}), 2.0,
                "clamp before first keyframe");
    expect_near(*evaluate_scalar_track(*hold, FrameIndex{100}), 8.0,
                "clamp after last keyframe");
  }

  // Segment lookup exposes half-open ownership.
  if (track) {
    const auto seg = find_track_segment(*track, FrameIndex{20});
    expect(seg.has_value() && seg->right.has_value(), "interior segment");
    if (seg && seg->right) {
      expect(seg->left.frame == FrameIndex{20}, "segment left at 20");
      expect(seg->right->frame == FrameIndex{40}, "segment right at 40");
    }
    const auto tail = find_track_segment(*track, FrameIndex{40});
    expect(tail.has_value() && !tail->right.has_value(),
           "last keyframe has no right");
  }

  // Validation edges.
  auto empty = make_scalar_track({});
  expect(!empty.has_value(), "reject empty track");
  auto unsorted = make_scalar_track({
      ScalarKeyframe{FrameIndex{3}, 1.0},
      ScalarKeyframe{FrameIndex{3}, 2.0},
  });
  expect(!unsorted.has_value(), "reject non-increasing frames");
  auto bad = make_scalar_track({
      ScalarKeyframe{FrameIndex{0}, std::numeric_limits<double>::quiet_NaN()},
  });
  expect(!bad.has_value(), "reject non-finite value");

  // Single keyframe is constant under random access.
  auto constant = make_scalar_track({ScalarKeyframe{FrameIndex{7}, 0.25}});
  expect(constant.has_value(), "single keyframe track");
  if (constant) {
    expect_near(*evaluate_scalar_track(*constant, FrameIndex{0}), 0.25,
                "constant before");
    expect_near(*evaluate_scalar_track(*constant, FrameIndex{7}), 0.25,
                "constant on");
    expect_near(*evaluate_scalar_track(*constant, FrameIndex{90}), 0.25,
                "constant after");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
