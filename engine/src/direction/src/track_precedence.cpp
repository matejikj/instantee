#include <instantee/direction/track_precedence.hpp>

#include <algorithm>
#include <map>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_conflict(std::string_view code,
                                 std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "track_precedence", message};
}

struct ChannelKey {
  std::string target_id;
  std::string property;

  [[nodiscard]] friend bool operator<(const ChannelKey &a,
                                      const ChannelKey &b) noexcept {
    if (a.target_id != b.target_id) {
      return a.target_id < b.target_id;
    }
    return a.property < b.property;
  }
};

[[nodiscard]] bool violates_keep_visible(const DirectedTrack &track) {
  if (track.property != "opacity") {
    return false;
  }
  for (const DirectedKeyframe &kf : track.keyframes) {
    if (kf.value < 1.0) {
      return true;
    }
  }
  return false;
}

} // namespace

std::expected<PrecedenceResolution, diagnostics::Error>
resolve_track_precedence_detailed(std::span<const TrackClaim> claims,
                                  std::span<const HardConstraint> constraints) {
  for (const TrackClaim &claim : claims) {
    if (claim.track.target_id.empty() || claim.track.property.empty()) {
      return std::unexpected(make_conflict(
          "invalid_track_claim", "Track claim is missing target or property"));
    }
    if (claim.track.keyframes.empty()) {
      return std::unexpected(make_conflict(
          "invalid_track_claim", "Track claim must include keyframes"));
    }
  }

  for (const HardConstraint &constraint : constraints) {
    if (constraint.target_id.empty() || constraint.property.empty()) {
      return std::unexpected(
          make_conflict("invalid_hard_constraint",
                        "Hard constraint is missing target or property"));
    }
    for (const TrackClaim &claim : claims) {
      if (claim.track.target_id != constraint.target_id ||
          claim.track.property != constraint.property) {
        continue;
      }
      if (constraint.kind == HardConstraintKind::forbid_animation) {
        return std::unexpected(make_conflict(
            "hard_direction_conflict", "Animation track conflicts with a hard "
                                       "forbid_animation constraint"));
      }
      if (constraint.kind == HardConstraintKind::keep_visible &&
          violates_keep_visible(claim.track)) {
        return std::unexpected(make_conflict(
            "hard_direction_conflict",
            "Animation track conflicts with a hard keep_visible constraint"));
      }
    }
  }

  std::map<ChannelKey, std::vector<const TrackClaim *>> by_channel;
  for (const TrackClaim &claim : claims) {
    by_channel[ChannelKey{claim.track.target_id, claim.track.property}]
        .push_back(&claim);
  }

  PrecedenceResolution resolution;
  resolution.tracks.reserve(by_channel.size());
  resolution.winners.reserve(by_channel.size());
  for (auto &[channel, group] : by_channel) {
    (void)channel;
    std::sort(group.begin(), group.end(),
              [](const TrackClaim *a, const TrackClaim *b) {
                return static_cast<std::uint8_t>(a->source) >
                       static_cast<std::uint8_t>(b->source);
              });
    const TrackClaim *winner = group.front();
    for (std::size_t index = 1; index < group.size(); ++index) {
      const TrackClaim *other = group[index];
      if (other->source == winner->source) {
        return std::unexpected(make_conflict(
            "hard_direction_conflict",
            "Duplicate track claims at the same precedence level"));
      }
      // Explicit author tracks must not be overwritten by presets/CDS. Treat
      // any lower-priority claim on the same channel as a hard conflict.
      if (winner->source == TrackSource::explicit_author) {
        return std::unexpected(make_conflict(
            "hard_direction_conflict", "Preset or CDS track would overwrite an "
                                       "explicit author animation"));
      }
      // Lower sources lose to a unique higher non-author source.
    }
    resolution.winners.push_back(*winner);
    resolution.tracks.push_back(winner->track);
  }

  std::sort(resolution.tracks.begin(), resolution.tracks.end(),
            [](const DirectedTrack &a, const DirectedTrack &b) {
              if (a.target_id != b.target_id) {
                return a.target_id < b.target_id;
              }
              if (a.property != b.property) {
                return a.property < b.property;
              }
              return a.id < b.id;
            });
  std::sort(resolution.winners.begin(), resolution.winners.end(),
            [](const TrackClaim &a, const TrackClaim &b) {
              if (a.track.target_id != b.track.target_id) {
                return a.track.target_id < b.track.target_id;
              }
              if (a.track.property != b.track.property) {
                return a.track.property < b.track.property;
              }
              return a.track.id < b.track.id;
            });
  return resolution;
}

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
resolve_track_precedence(std::span<const TrackClaim> claims,
                         std::span<const HardConstraint> constraints) {
  auto detailed = resolve_track_precedence_detailed(claims, constraints);
  if (!detailed) {
    return std::unexpected(detailed.error());
  }
  return std::move(detailed->tracks);
}

} // namespace instantee::direction
