#include <instantee/direction/directed_film.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "directed_film", message};
}

[[nodiscard]] std::uint64_t
resolve_span(const SemanticIntent &intent,
             std::uint64_t shot_duration_frames) noexcept {
  if (shot_duration_frames == 0) {
    return 0;
  }
  std::uint64_t span = shot_duration_frames;
  if (intent.duration_frames.has_value()) {
    span =
        std::min<std::uint64_t>(*intent.duration_frames, shot_duration_frames);
  } else {
    // Intensity scales the creative span inside the author shot budget.
    const double scaled = static_cast<double>(shot_duration_frames) *
                          std::clamp(0.25 + 0.75 * intent.intensity, 0.25, 1.0);
    span = static_cast<std::uint64_t>(scaled);
    if (span == 0) {
      span = 1;
    }
    span = std::min(span, shot_duration_frames);
  }
  return span;
}

[[nodiscard]] DirectedTrack
opacity_track(std::string_view shot_id, std::string_view target_id,
              std::size_t ordinal, std::uint64_t start, std::uint64_t end,
              double from, double to) {
  DirectedTrack track;
  track.id = std::string(shot_id) + "_trk_" + std::string(target_id) + "_" +
             std::to_string(ordinal);
  track.target_id = std::string(target_id);
  track.property = "opacity";
  track.keyframes.push_back(DirectedKeyframe{.frame = start, .value = from});
  track.keyframes.push_back(DirectedKeyframe{.frame = end, .value = to});
  return track;
}

} // namespace

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_semantic_intent(const SemanticIntent &intent,
                      std::uint64_t shot_duration_frames,
                      std::string_view shot_id) {
  if (shot_duration_frames == 0) {
    return std::unexpected(
        make_invalid("invalid_shot_duration",
                     "Shot duration_frames must be greater than 0"));
  }
  if (intent.targets.empty()) {
    return std::unexpected(make_invalid("insufficient_intent_targets",
                                        "Intent has no targets to lower"));
  }

  const std::uint64_t span = resolve_span(intent, shot_duration_frames);
  const std::uint64_t end =
      span == 0 ? 0 : std::min(span - 1, shot_duration_frames - 1);

  std::vector<DirectedTrack> tracks;
  tracks.reserve(intent.targets.size());

  switch (intent.kind) {
  case IntentKind::reveal:
  case IntentKind::explain: {
    const double peak = std::clamp(intent.intensity, 0.0, 1.0);
    for (std::size_t i = 0; i < intent.targets.size(); ++i) {
      tracks.push_back(opacity_track(shot_id, intent.targets[i].value(), i, 0,
                                     end, 0.0, peak));
    }
    break;
  }
  case IntentKind::focus: {
    const double peak = std::clamp(0.5 + 0.5 * intent.intensity, 0.5, 1.0);
    for (std::size_t i = 0; i < intent.targets.size(); ++i) {
      tracks.push_back(opacity_track(shot_id, intent.targets[i].value(), i, 0,
                                     end, peak * 0.35, peak));
    }
    break;
  }
  case IntentKind::compare: {
    const double peak = std::clamp(intent.intensity, 0.0, 1.0);
    const std::uint64_t stagger = std::max<std::uint64_t>(
        1, end / static_cast<std::uint64_t>(intent.targets.size()));
    for (std::size_t i = 0; i < intent.targets.size(); ++i) {
      const std::uint64_t local_start =
          std::min<std::uint64_t>(i * stagger, end);
      tracks.push_back(opacity_track(shot_id, intent.targets[i].value(), i,
                                     local_start, end, 0.0, peak));
    }
    break;
  }
  case IntentKind::build_suspense: {
    const double peak = std::clamp(intent.intensity, 0.0, 1.0);
    const std::uint64_t hold = end / 2;
    for (std::size_t i = 0; i < intent.targets.size(); ++i) {
      DirectedTrack track;
      track.id = std::string(shot_id) + "_trk_" +
                 std::string(intent.targets[i].value()) + "_" +
                 std::to_string(i);
      track.target_id = std::string(intent.targets[i].value());
      track.property = "opacity";
      track.keyframes.push_back(
          DirectedKeyframe{.frame = 0, .value = peak * 0.15});
      track.keyframes.push_back(
          DirectedKeyframe{.frame = hold, .value = peak * 0.25});
      track.keyframes.push_back(DirectedKeyframe{.frame = end, .value = peak});
      tracks.push_back(std::move(track));
    }
    break;
  }
  }

  if (tracks.empty()) {
    return std::unexpected(make_invalid(
        "unsupported_intent", "CDS has no lowering for this semantic intent"));
  }
  return tracks;
}

} // namespace instantee::direction
