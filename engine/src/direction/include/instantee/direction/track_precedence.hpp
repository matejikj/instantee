#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/directed_film.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// SAS §9.3 precedence (highest wins). Hard constraints are not a track source;
// they veto conflicting claims with a compile error.
enum class TrackSource : std::uint8_t {
  framework_default = 1,
  style_bible = 2,
  shot_override = 3,
  explicit_author = 5,
};

struct TrackClaim {
  DirectedTrack track;
  TrackSource source = TrackSource::framework_default;
};

enum class HardConstraintKind : std::uint8_t {
  forbid_animation = 1,
  keep_visible,
};

struct HardConstraint {
  std::string target_id;
  std::string property;
  HardConstraintKind kind = HardConstraintKind::forbid_animation;
};

// Winning claims retain their TrackSource for audit/direction_report.
struct PrecedenceResolution {
  std::vector<DirectedTrack> tracks;
  std::vector<TrackClaim> winners;
};

// Resolve claims by precedence. Explicit author tracks are never overwritten:
// a lower-priority claim on the same (target, property) is a hard conflict.
// Hard constraints that forbid animation or require keep_visible also fail
// closed when violated.
[[nodiscard]] std::expected<PrecedenceResolution, diagnostics::Error>
resolve_track_precedence_detailed(
    std::span<const TrackClaim> claims,
    std::span<const HardConstraint> constraints = {});

[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
resolve_track_precedence(std::span<const TrackClaim> claims,
                         std::span<const HardConstraint> constraints = {});

[[nodiscard]] constexpr std::string_view
track_source_id(TrackSource source) noexcept {
  switch (source) {
  case TrackSource::framework_default:
    return "framework_default";
  case TrackSource::style_bible:
    return "style_bible";
  case TrackSource::shot_override:
    return "shot_override";
  case TrackSource::explicit_author:
    return "explicit_author";
  }
  return "unknown";
}

} // namespace instantee::direction
