#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace instantee::runtime {

// Per-segment sampler. Cubic-bezier inversion is intentionally out of scope
// here; hold and linear cover segment lookup and exact frame sampling.
enum class TrackInterpolation : std::uint8_t { hold = 0, linear };

// Outside the first/last keyframe the sampler clamps to the endpoint value.
// Tracks are authored in absolute film frames and are shared across shots
// without remapping into shot-local time.
enum class TrackEndBehavior : std::uint8_t { clamp = 0 };

struct ScalarKeyframe {
  core::FrameIndex frame{};
  double value = 0.0;
};

struct ScalarTrack {
  std::vector<ScalarKeyframe> keyframes;
  TrackInterpolation interpolation = TrackInterpolation::linear;
  TrackEndBehavior end_behavior = TrackEndBehavior::clamp;
};

// Half-open segment [left, right) when right is present. When right is absent
// the sample sits on (or after) the last keyframe and uses left only.
struct TrackSegment {
  ScalarKeyframe left{};
  std::optional<ScalarKeyframe> right;
};

// Validates non-empty, strictly increasing frames. Rejects non-finite values.
[[nodiscard]] std::expected<ScalarTrack, diagnostics::Error>
make_scalar_track(std::vector<ScalarKeyframe> keyframes,
                  TrackInterpolation interpolation = TrackInterpolation::linear,
                  TrackEndBehavior end_behavior = TrackEndBehavior::clamp);

// Locates the segment that owns `frame` under clamp end behavior.
[[nodiscard]] std::expected<TrackSegment, diagnostics::Error>
find_track_segment(const ScalarTrack &track, core::FrameIndex frame);

// Random-access sample. Independent of evaluation order and of shot cuts:
// absolute film frames drive the lookup.
[[nodiscard]] std::expected<double, diagnostics::Error>
evaluate_scalar_track(const ScalarTrack &track, core::FrameIndex frame);

} // namespace instantee::runtime
