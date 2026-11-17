#include <instantee/runtime/timeline.hpp>

#include <cmath>
#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

bool finite_value(double value) noexcept { return std::isfinite(value); }

double unit_time(core::FrameIndex frame, const ScalarKeyframe &left,
                 const ScalarKeyframe &right) noexcept {
  // Convert before subtracting so extrapolation before the first keyframe
  // does not wrap uint64 (frame < left.frame).
  const double span = static_cast<double>(right.frame.value()) -
                      static_cast<double>(left.frame.value());
  // Strictly increasing keyframes guarantee span >= 1.
  const double delta = static_cast<double>(frame.value()) -
                       static_cast<double>(left.frame.value());
  return delta / span;
}

double linear_map(double unit, const ScalarKeyframe &left,
                  const ScalarKeyframe &right) noexcept {
  return left.value + (right.value - left.value) * unit;
}

std::expected<double, diagnostics::Error>
sample_segment(const ScalarTrack &track, const TrackSegment &segment,
               core::FrameIndex frame) {
  if (!segment.right.has_value()) {
    return segment.left.value;
  }
  const ScalarKeyframe &left = segment.left;
  const ScalarKeyframe &right = *segment.right;
  if (frame == left.frame) {
    return left.value;
  }
  if (track.interpolation == TrackInterpolation::hold) {
    return left.value;
  }

  const double u = unit_time(frame, left, right);
  if (track.interpolation == TrackInterpolation::linear) {
    return linear_map(u, left, right);
  }

  // cubic_bezier: ease unit time, then lerp values. Endpoints of the unit
  // easing are exact so the value curve meets keyframes without NaN.
  if (!track.bezier.has_value()) {
    return std::unexpected(
        err("missing_bezier_controls",
            "cubic_bezier interpolation requires control points"));
  }
  const double eased = sample_cubic_bezier(*track.bezier, u);
  if (!finite_value(eased)) {
    return std::unexpected(
        err("non_finite_easing", "Cubic-bezier sample was not finite"));
  }
  return linear_map(eased, left, right);
}

std::expected<double, diagnostics::Error>
evaluate_extrapolated(const ScalarTrack &track, core::FrameIndex frame) {
  const auto &keys = track.keyframes;
  if (keys.size() < 2) {
    return keys.front().value;
  }
  if (track.interpolation == TrackInterpolation::hold) {
    // Hold has no slope outside the span; clamp is the documented policy.
    if (frame < keys.front().frame) {
      return keys.front().value;
    }
    return keys.back().value;
  }

  if (frame < keys.front().frame) {
    const double u = unit_time(frame, keys[0], keys[1]);
    return linear_map(u, keys[0], keys[1]);
  }
  const ScalarKeyframe &left = keys[keys.size() - 2];
  const ScalarKeyframe &right = keys.back();
  const double u = unit_time(frame, left, right);
  return linear_map(u, left, right);
}

} // namespace

std::expected<ScalarTrack, diagnostics::Error> make_scalar_track(
    std::vector<ScalarKeyframe> keyframes, TrackInterpolation interpolation,
    TrackEndBehavior end_behavior, std::optional<CubicBezierControls> bezier) {
  if (keyframes.empty()) {
    return std::unexpected(
        err("empty_track", "Animation track requires at least one keyframe"));
  }
  for (std::size_t index = 0; index < keyframes.size(); ++index) {
    if (!finite_value(keyframes[index].value)) {
      return std::unexpected(
          err("non_finite_value", "Keyframe value must be finite"));
    }
    if (index == 0) {
      continue;
    }
    if (keyframes[index].frame <= keyframes[index - 1].frame) {
      return std::unexpected(
          err("non_increasing_keyframes",
              "Keyframes must be strictly increasing by frame"));
    }
  }
  if (interpolation == TrackInterpolation::cubic_bezier) {
    if (!bezier.has_value()) {
      return std::unexpected(
          err("missing_bezier_controls",
              "cubic_bezier interpolation requires control points"));
    }
  } else if (bezier.has_value()) {
    return std::unexpected(
        err("unexpected_bezier_controls",
            "Bezier controls are only valid with cubic_bezier interpolation"));
  }

  ScalarTrack track;
  track.keyframes = std::move(keyframes);
  track.interpolation = interpolation;
  track.end_behavior = end_behavior;
  track.bezier = bezier;
  return track;
}

std::expected<TrackSegment, diagnostics::Error>
find_track_segment(const ScalarTrack &track, core::FrameIndex frame) {
  if (track.keyframes.empty()) {
    return std::unexpected(
        err("empty_track", "Animation track requires at least one keyframe"));
  }

  const auto &keys = track.keyframes;
  // Clamp before the first keyframe.
  if (frame < keys.front().frame) {
    return TrackSegment{.left = keys.front(), .right = std::nullopt};
  }
  // Clamp on/after the last keyframe (end-exclusive segments end here).
  if (frame >= keys.back().frame) {
    return TrackSegment{.left = keys.back(), .right = std::nullopt};
  }

  // Binary search for the greatest keyframe with frame <= sample frame.
  std::size_t lo = 0;
  std::size_t hi = keys.size() - 1;
  while (lo + 1 < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (keys[mid].frame <= frame) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  // keys[lo].frame <= frame < keys[hi].frame  (half-open segment).
  return TrackSegment{.left = keys[lo], .right = keys[hi]};
}

std::expected<double, diagnostics::Error>
evaluate_scalar_track(const ScalarTrack &track, core::FrameIndex frame) {
  if (track.keyframes.empty()) {
    return std::unexpected(
        err("empty_track", "Animation track requires at least one keyframe"));
  }
  const auto &keys = track.keyframes;
  const bool outside = frame < keys.front().frame || frame > keys.back().frame;
  if (outside && track.end_behavior == TrackEndBehavior::extrapolate_linear) {
    return evaluate_extrapolated(track, frame);
  }

  auto segment = find_track_segment(track, frame);
  if (!segment) {
    return std::unexpected(segment.error());
  }
  return sample_segment(track, *segment, frame);
}

} // namespace instantee::runtime
