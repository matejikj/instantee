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

std::expected<double, diagnostics::Error>
lerp_hold_or_linear(TrackInterpolation interpolation,
                    const TrackSegment &segment, core::FrameIndex frame) {
  if (!segment.right.has_value()) {
    return segment.left.value;
  }
  const ScalarKeyframe &left = segment.left;
  const ScalarKeyframe &right = *segment.right;
  if (frame == left.frame) {
    return left.value;
  }
  if (interpolation == TrackInterpolation::hold) {
    return left.value;
  }

  const std::uint64_t span = right.frame.value() - left.frame.value();
  // Strictly increasing keyframes guarantee span >= 1.
  const double t = static_cast<double>(frame.value() - left.frame.value()) /
                   static_cast<double>(span);
  return left.value + (right.value - left.value) * t;
}

} // namespace

std::expected<ScalarTrack, diagnostics::Error>
make_scalar_track(std::vector<ScalarKeyframe> keyframes,
                  TrackInterpolation interpolation,
                  TrackEndBehavior end_behavior) {
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
  ScalarTrack track;
  track.keyframes = std::move(keyframes);
  track.interpolation = interpolation;
  track.end_behavior = end_behavior;
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
  // Clamp on/after the last keyframe.
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
  auto segment = find_track_segment(track, frame);
  if (!segment) {
    return std::unexpected(segment.error());
  }
  return lerp_hold_or_linear(track.interpolation, *segment, frame);
}

} // namespace instantee::runtime
