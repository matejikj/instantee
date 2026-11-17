#include <instantee/direction/track_precedence.hpp>

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

using instantee::direction::DirectedKeyframe;
using instantee::direction::DirectedTrack;
using instantee::direction::HardConstraint;
using instantee::direction::HardConstraintKind;
using instantee::direction::resolve_track_precedence;
using instantee::direction::TrackClaim;
using instantee::direction::TrackSource;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

DirectedTrack make_opacity(std::string_view id, std::string_view target,
                           double from, double to) {
  DirectedTrack track;
  track.id = std::string(id);
  track.target_id = std::string(target);
  track.property = "opacity";
  track.keyframes = {DirectedKeyframe{.frame = 0, .value = from},
                     DirectedKeyframe{.frame = 10, .value = to}};
  return track;
}

} // namespace

int main() {
  // Matrix: Style Bible wins over framework default on the same channel.
  {
    std::array<TrackClaim, 2> claims{{
        {.track = make_opacity("fw", "n0", 0.0, 0.2),
         .source = TrackSource::framework_default},
        {.track = make_opacity("bible", "n0", 0.0, 1.0),
         .source = TrackSource::style_bible},
    }};
    auto resolved = resolve_track_precedence(claims);
    expect(resolved.has_value(), "bible over framework");
    if (resolved) {
      expect(resolved->size() == 1, "one channel");
      expect(resolved->front().id == "bible", "kept bible track");
    }
  }

  // Matrix: shot override wins over Style Bible.
  {
    std::array<TrackClaim, 2> claims{{
        {.track = make_opacity("bible", "n0", 0.0, 0.5),
         .source = TrackSource::style_bible},
        {.track = make_opacity("shot", "n0", 0.0, 0.9),
         .source = TrackSource::shot_override},
    }};
    auto resolved = resolve_track_precedence(claims);
    expect(resolved.has_value(), "shot over bible");
    if (resolved) {
      expect(resolved->front().id == "shot", "kept shot override");
    }
  }

  // Matrix: explicit author vs CDS/preset is a hard conflict (never overwrite).
  {
    std::array<TrackClaim, 2> claims{{
        {.track = make_opacity("author", "headline", 1.0, 1.0),
         .source = TrackSource::explicit_author},
        {.track = make_opacity("cds", "headline", 0.0, 1.0),
         .source = TrackSource::style_bible},
    }};
    auto conflict = resolve_track_precedence(claims);
    expect(!conflict.has_value(), "author vs cds conflict");
    if (!conflict) {
      expect(conflict.error().code == "hard_direction_conflict",
             "hard conflict code");
    }
  }

  // Matrix: hard forbid_animation constraint vs any track.
  {
    std::array<TrackClaim, 1> claims{{
        {.track = make_opacity("cds", "badge", 0.0, 1.0),
         .source = TrackSource::style_bible},
    }};
    std::array<HardConstraint, 1> constraints{{
        {.target_id = "badge",
         .property = "opacity",
         .kind = HardConstraintKind::forbid_animation},
    }};
    auto conflict = resolve_track_precedence(claims, constraints);
    expect(!conflict.has_value(), "forbid_animation conflict");
    if (!conflict) {
      expect(conflict.error().code == "hard_direction_conflict", "forbid code");
    }
  }

  // Matrix: keep_visible rejects opacity dips.
  {
    std::array<TrackClaim, 1> claims{{
        {.track = make_opacity("cds", "logo", 0.2, 1.0),
         .source = TrackSource::framework_default},
    }};
    std::array<HardConstraint, 1> constraints{{
        {.target_id = "logo",
         .property = "opacity",
         .kind = HardConstraintKind::keep_visible},
    }};
    auto conflict = resolve_track_precedence(claims, constraints);
    expect(!conflict.has_value(), "keep_visible conflict");
  }

  // Distinct channels coexist.
  {
    std::array<TrackClaim, 2> claims{{
        {.track = make_opacity("a", "n0", 0.0, 1.0),
         .source = TrackSource::explicit_author},
        {.track = make_opacity("b", "n1", 0.0, 1.0),
         .source = TrackSource::style_bible},
    }};
    auto resolved = resolve_track_precedence(claims);
    expect(resolved.has_value() && resolved->size() == 2, "two channels");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
