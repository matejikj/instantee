#include <instantee/direction/editorial_beats.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "editorial_beats", message};
}

[[nodiscard]] bool
durations_cover_total(std::span<const ScheduledEditorialBeat> beats,
                      std::uint64_t total) noexcept {
  if (beats.empty()) {
    return false;
  }
  std::uint64_t sum = 0;
  std::uint64_t expected = 0;
  for (const auto &beat : beats) {
    if (beat.start_frame != expected || beat.duration_frames == 0) {
      return false;
    }
    if (sum >
        std::numeric_limits<std::uint64_t>::max() - beat.duration_frames) {
      return false;
    }
    sum += beat.duration_frames;
    expected += beat.duration_frames;
  }
  return sum == total;
}

[[nodiscard]] std::vector<DirectedTrack>
build_tracks(std::span<const ScheduledEditorialBeat> beats) {
  std::vector<DirectedTrack> tracks;
  for (const auto &beat : beats) {
    if (beat.track_id.empty() || beat.target_id.empty()) {
      continue;
    }
    DirectedTrack track;
    track.id = beat.track_id;
    track.target_id = beat.target_id;
    track.property = beat.property.empty() ? "opacity" : beat.property;
    // Gate: off before the beat, on at beat start, still on at beat end-1.
    if (beat.start_frame > 0) {
      track.keyframes.push_back(
          DirectedKeyframe{.frame = beat.start_frame - 1, .value = 0.0});
    }
    track.keyframes.push_back(
        DirectedKeyframe{.frame = beat.start_frame, .value = 1.0});
    const std::uint64_t end = beat.start_frame + beat.duration_frames;
    if (end > beat.start_frame) {
      track.keyframes.push_back(
          DirectedKeyframe{.frame = end - 1, .value = 1.0});
    }
    tracks.push_back(std::move(track));
  }
  return tracks;
}

[[nodiscard]] std::expected<std::vector<std::uint64_t>, diagnostics::Error>
pack_durations(std::span<const std::uint64_t> weights, std::uint64_t total) {
  if (weights.empty()) {
    return std::unexpected(
        make_invalid("empty_beats", "editorial beat schedule requires beats"));
  }
  if (total == 0) {
    return std::unexpected(make_invalid(
        "invalid_total", "total_duration_frames must be greater than 0"));
  }
  if (weights.size() > total) {
    return std::unexpected(make_invalid(
        "budget_too_small",
        "total_duration_frames must leave at least one frame per beat"));
  }

  std::uint64_t weight_sum = 0;
  for (std::uint64_t w : weights) {
    if (w == 0) {
      return std::unexpected(
          make_invalid("invalid_weight", "beat weight must be greater than 0"));
    }
    if (weight_sum > std::numeric_limits<std::uint64_t>::max() - w) {
      return std::unexpected(
          make_invalid("weight_overflow", "beat weights overflow"));
    }
    weight_sum += w;
  }

  // Each beat gets a floor share, then remainders are assigned left-to-right
  // so the sum matches `total` exactly and the schedule stays deterministic.
  std::vector<std::uint64_t> durations(weights.size(), 0);
  std::uint64_t assigned = 0;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    durations[i] = (total * weights[i]) / weight_sum;
    if (durations[i] == 0) {
      durations[i] = 1;
    }
    assigned += durations[i];
  }

  if (assigned > total) {
    // Trim overflow from the right while keeping every beat >= 1.
    std::uint64_t overflow = assigned - total;
    for (std::size_t i = durations.size(); i-- > 0;) {
      if (overflow == 0) {
        break;
      }
      const std::uint64_t reducible = durations[i] - 1;
      const std::uint64_t take = std::min(reducible, overflow);
      durations[i] -= take;
      overflow -= take;
    }
    if (overflow != 0) {
      return std::unexpected(
          make_invalid("unsatisfiable_pack",
                       "cannot pack beats into total_duration_frames"));
    }
  } else if (assigned < total) {
    std::uint64_t remain = total - assigned;
    for (std::size_t i = 0; i < durations.size() && remain > 0; ++i) {
      durations[i] += 1;
      --remain;
    }
    // If still remaining (more remainder than beats), dump onto the last beat.
    if (remain > 0) {
      durations.back() += remain;
    }
  }

  std::uint64_t sum = 0;
  for (std::uint64_t d : durations) {
    sum += d;
  }
  if (sum != total) {
    return std::unexpected(make_invalid(
        "pack_invariant", "packed beat durations must equal total budget"));
  }
  return durations;
}

[[nodiscard]] EditorialBeatSchedule
assemble_schedule(std::uint64_t total,
                  std::span<const EditorialBeatMarker> markers,
                  std::span<const std::uint64_t> durations) {
  EditorialBeatSchedule out;
  out.total_duration_frames = total;
  out.beats.reserve(markers.size());
  std::uint64_t cursor = 0;
  for (std::size_t i = 0; i < markers.size(); ++i) {
    ScheduledEditorialBeat beat;
    beat.kind = markers[i].kind;
    beat.start_frame = cursor;
    beat.duration_frames = durations[i];
    beat.track_id = markers[i].track_id;
    beat.target_id = markers[i].target_id;
    beat.property = markers[i].property;
    cursor += durations[i];
    out.beats.push_back(std::move(beat));
  }
  out.tracks = build_tracks(out.beats);
  return out;
}

} // namespace

std::expected<EditorialBeatSchedule, diagnostics::Error>
schedule_editorial_beats(const EditorialBeatScheduleRequest &request) {
  if (request.beats.empty()) {
    return std::unexpected(
        make_invalid("empty_beats", "editorial beat schedule requires beats"));
  }

  std::vector<std::uint64_t> weights;
  weights.reserve(request.beats.size());
  for (const auto &marker : request.beats) {
    weights.push_back(marker.weight);
  }

  auto durations = pack_durations(weights, request.total_duration_frames);
  if (!durations) {
    return std::unexpected(durations.error());
  }

  auto schedule = assemble_schedule(request.total_duration_frames,
                                    request.beats, *durations);
  if (!durations_cover_total(schedule.beats, schedule.total_duration_frames)) {
    return std::unexpected(
        make_invalid("schedule_invariant",
                     "scheduled beats must abut and cover the total duration"));
  }
  return schedule;
}

std::expected<EditorialBeatSchedule, diagnostics::Error>
rescale_editorial_beat(const EditorialBeatSchedule &schedule, std::size_t index,
                       std::uint64_t new_duration_frames) {
  if (schedule.beats.empty()) {
    return std::unexpected(
        make_invalid("empty_beats", "cannot rescale an empty beat schedule"));
  }
  if (index >= schedule.beats.size()) {
    return std::unexpected(
        make_invalid("beat_index_oob", "beat index is out of range"));
  }
  if (index + 1 == schedule.beats.size()) {
    return std::unexpected(make_invalid(
        "no_subsequent_beats",
        "changing the last beat has no subsequent events to reschedule"));
  }
  if (new_duration_frames == 0) {
    return std::unexpected(make_invalid(
        "invalid_duration", "new beat duration must be greater than 0"));
  }

  const std::uint64_t total = schedule.total_duration_frames;
  const std::size_t subsequent = schedule.beats.size() - index - 1;
  if (new_duration_frames > total - subsequent) {
    return std::unexpected(make_invalid(
        "duration_leaves_no_room",
        "new beat duration must leave at least one frame for each subsequent "
        "beat"));
  }

  // Prefix before `index` stays fixed; only subsequent beats are rebuilt.
  std::uint64_t prefix = 0;
  for (std::size_t i = 0; i < index; ++i) {
    prefix += schedule.beats[i].duration_frames;
  }
  if (prefix > total - new_duration_frames - subsequent) {
    return std::unexpected(make_invalid(
        "prefix_overflow", "prefix beats leave insufficient room for rescale"));
  }

  const std::uint64_t remaining =
      total - prefix - new_duration_frames; // for subsequent beats

  std::vector<std::uint64_t> subsequent_weights;
  subsequent_weights.reserve(subsequent);
  for (std::size_t i = index + 1; i < schedule.beats.size(); ++i) {
    // Preserve relative proportions of the previous subsequent durations.
    subsequent_weights.push_back(schedule.beats[i].duration_frames);
  }
  auto subsequent_durations = pack_durations(subsequent_weights, remaining);
  if (!subsequent_durations) {
    return std::unexpected(subsequent_durations.error());
  }

  // Rebuild markers from the existing schedule so track bindings survive.
  std::vector<EditorialBeatMarker> markers;
  markers.reserve(schedule.beats.size());
  std::vector<std::uint64_t> durations;
  durations.reserve(schedule.beats.size());
  for (std::size_t i = 0; i < schedule.beats.size(); ++i) {
    const auto &src = schedule.beats[i];
    EditorialBeatMarker marker;
    marker.kind = src.kind;
    marker.weight = 1; // unused once durations are explicit
    marker.track_id = src.track_id;
    marker.target_id = src.target_id;
    marker.property = src.property;
    markers.push_back(std::move(marker));
    if (i < index) {
      durations.push_back(src.duration_frames);
    } else if (i == index) {
      durations.push_back(new_duration_frames);
    } else {
      durations.push_back((*subsequent_durations)[i - index - 1]);
    }
  }

  auto out = assemble_schedule(total, markers, durations);
  if (out.total_duration_frames != schedule.total_duration_frames) {
    return std::unexpected(make_invalid(
        "total_changed", "rescaling beats must not change the total duration"));
  }
  if (!durations_cover_total(out.beats, out.total_duration_frames)) {
    return std::unexpected(
        make_invalid("schedule_invariant",
                     "rescaled beats must abut and cover the total duration"));
  }
  return out;
}

std::expected<std::uint64_t, diagnostics::Error>
editorial_beat_track_offset(const EditorialBeatSchedule &schedule,
                            std::size_t beat_index, std::uint64_t local_frame) {
  if (beat_index >= schedule.beats.size()) {
    return std::unexpected(
        make_invalid("beat_index_oob", "beat index is out of range"));
  }
  const auto &beat = schedule.beats[beat_index];
  if (local_frame >= beat.duration_frames) {
    return std::unexpected(make_invalid(
        "local_frame_oob", "local_frame must be inside the beat duration"));
  }
  if (beat.start_frame >
      std::numeric_limits<std::uint64_t>::max() - local_frame) {
    return std::unexpected(
        make_invalid("offset_overflow", "track offset would overflow"));
  }
  return beat.start_frame + local_frame;
}

} // namespace instantee::direction
