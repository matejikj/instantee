#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/easing.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace instantee::runtime {

// Step holds the left keyframe on a half-open segment [left, right). Linear
// and cubic_bezier map unit time across that segment; bezier needs controls.
enum class TrackInterpolation : std::uint8_t {
  hold = 0, // step
  linear,
  cubic_bezier
};

// End policy outside the authored keyframe span:
// - clamp: endpoint value (default)
// - extrapolate_linear: extend the nearest segment as a linear frame→value
//   map (easing is not reapplied outside the span). Hold and single-keyframe
//   tracks still clamp.
enum class TrackEndBehavior : std::uint8_t { clamp = 0, extrapolate_linear };

struct ScalarKeyframe {
  core::FrameIndex frame{};
  double value = 0.0;
};

struct ScalarTrack {
  std::vector<ScalarKeyframe> keyframes;
  TrackInterpolation interpolation = TrackInterpolation::linear;
  TrackEndBehavior end_behavior = TrackEndBehavior::clamp;
  std::optional<CubicBezierControls> bezier;
};

// Half-open segment [left, right) when right is present. When right is absent
// the sample sits on (or after) the last keyframe under clamp policy.
struct TrackSegment {
  ScalarKeyframe left{};
  std::optional<ScalarKeyframe> right;
};

[[nodiscard]] std::expected<ScalarTrack, diagnostics::Error>
make_scalar_track(std::vector<ScalarKeyframe> keyframes,
                  TrackInterpolation interpolation = TrackInterpolation::linear,
                  TrackEndBehavior end_behavior = TrackEndBehavior::clamp,
                  std::optional<CubicBezierControls> bezier = std::nullopt);

[[nodiscard]] std::expected<TrackSegment, diagnostics::Error>
find_track_segment(const ScalarTrack &track, core::FrameIndex frame);

[[nodiscard]] std::expected<double, diagnostics::Error>
evaluate_scalar_track(const ScalarTrack &track, core::FrameIndex frame);

} // namespace instantee::runtime
