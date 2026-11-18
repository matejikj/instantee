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

[[nodiscard]] std::uint64_t resolve_span(const SemanticIntent &intent,
                                         std::uint64_t shot_duration_frames,
                                         double duration_scale) noexcept {
  if (shot_duration_frames == 0) {
    return 0;
  }
  if (intent.duration_frames.has_value()) {
    return std::min<std::uint64_t>(*intent.duration_frames,
                                   shot_duration_frames);
  }
  const double scaled = static_cast<double>(shot_duration_frames) *
                        std::clamp(duration_scale, 0.05, 1.0);
  std::uint64_t span = static_cast<std::uint64_t>(scaled);
  if (span == 0) {
    span = 1;
  }
  return std::min(span, shot_duration_frames);
}

[[nodiscard]] double peak_for(const SemanticIntent &intent,
                              const MotionGrammarBinding &binding) noexcept {
  return std::clamp(intent.intensity * binding.intensity_scale, 0.0, 1.0);
}

} // namespace

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_semantic_intent(const SemanticIntent &intent,
                      std::uint64_t shot_duration_frames,
                      std::string_view shot_id, std::string_view package_id,
                      const MotionGrammarRegistry &grammar,
                      const std::optional<CreativeSeedContext> &seed) {
  if (shot_duration_frames == 0) {
    return std::unexpected(
        make_invalid("invalid_shot_duration",
                     "Shot duration_frames must be greater than 0"));
  }
  if (intent.targets.empty()) {
    return std::unexpected(make_invalid("insufficient_intent_targets",
                                        "Intent has no targets to lower"));
  }
  if (package_id.empty()) {
    return std::unexpected(
        make_invalid("invalid_package_id", "Style package_id is required"));
  }

  auto base_binding = grammar.resolve(package_id, intent.kind);
  if (!base_binding) {
    return std::unexpected(base_binding.error());
  }

  std::vector<DirectedTrack> tracks;
  tracks.reserve(intent.targets.size());

  for (std::size_t i = 0; i < intent.targets.size(); ++i) {
    MotionGrammarBinding binding = *base_binding;
    if (seed.has_value()) {
      VariationIdentity identity;
      identity.film_seed = seed->film_seed;
      identity.package_id = std::string(package_id);
      identity.package_version = std::string(seed->package_version);
      identity.shot_id = std::string(shot_id);
      identity.object_id = std::string(intent.targets[i].value());
      auto variation = sample_creative_variation(identity, seed->limits);
      if (!variation) {
        return std::unexpected(variation.error());
      }
      auto varied = apply_creative_variation(binding, *variation);
      if (!varied) {
        return std::unexpected(varied.error());
      }
      binding = std::move(*varied);
    }

    const std::uint64_t span =
        resolve_span(intent, shot_duration_frames, binding.duration_scale);
    const std::uint64_t end =
        span == 0 ? 0 : std::min(span - 1, shot_duration_frames - 1);
    const double peak = peak_for(intent, binding);
    const std::uint64_t stagger = binding.stagger_frames;

    DirectedTrack track;
    track.id = std::string(shot_id) + "_trk_" +
               std::string(intent.targets[i].value()) + "_" + std::to_string(i);
    track.target_id = std::string(intent.targets[i].value());
    track.property = binding.property;

    const std::uint64_t local_start =
        (binding.pattern == GesturePattern::stagger)
            ? std::min<std::uint64_t>(i * std::max<std::uint64_t>(stagger, 1),
                                      end)
            : 0;

    track.keyframes.push_back(
        DirectedKeyframe{.frame = local_start,
                         .value = std::clamp(binding.from_value, 0.0, 1.0)});

    if (binding.has_hold) {
      const std::uint64_t hold_frame = std::clamp<std::uint64_t>(
          local_start + static_cast<std::uint64_t>(
                            static_cast<double>(end - local_start) *
                            std::clamp(binding.hold_fraction, 0.0, 1.0)),
          local_start, end);
      track.keyframes.push_back(
          DirectedKeyframe{.frame = hold_frame,
                           .value = std::clamp(binding.hold_value, 0.0, 1.0)});
    }

    track.keyframes.push_back(DirectedKeyframe{.frame = end, .value = peak});
    tracks.push_back(std::move(track));
  }

  if (tracks.empty()) {
    return std::unexpected(make_invalid(
        "unsupported_intent", "CDS has no lowering for this semantic intent"));
  }
  return tracks;
}

} // namespace instantee::direction
