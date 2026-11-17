#include <instantee/direction/transition_planner.hpp>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "transition_planner", message};
}

[[nodiscard]] bool is_cut(TransitionKind kind) noexcept {
  return kind == TransitionKind::hard_cut || kind == TransitionKind::match_cut;
}

[[nodiscard]] std::string blend_for(TransitionKind kind) {
  switch (kind) {
  case TransitionKind::hard_cut:
  case TransitionKind::match_cut:
    return "cut";
  case TransitionKind::wipe:
    return "wipe";
  case TransitionKind::reveal:
    return "dissolve";
  }
  return "cut";
}

struct ShotRef {
  std::uint32_t index = 0;
  const ShotTiming *timing = nullptr;
};

} // namespace

std::expected<TransitionKind, diagnostics::Error>
parse_transition_kind(std::string_view text) {
  if (text == "hard_cut") {
    return TransitionKind::hard_cut;
  }
  if (text == "match_cut") {
    return TransitionKind::match_cut;
  }
  if (text == "wipe") {
    return TransitionKind::wipe;
  }
  if (text == "reveal") {
    return TransitionKind::reveal;
  }
  return std::unexpected(
      make_error("unknown_transition_kind", "Unknown transition kind"));
}

std::expected<TransitionKind, diagnostics::Error>
transition_kind_from_authoring_blend(std::string_view blend) {
  if (blend == "cut") {
    return TransitionKind::hard_cut;
  }
  if (blend == "wipe") {
    return TransitionKind::wipe;
  }
  if (blend == "dissolve") {
    return TransitionKind::reveal;
  }
  return std::unexpected(
      make_error("unknown_authoring_blend",
                 "Authoring transition blend must be cut, dissolve, or wipe"));
}

std::expected<TransitionPlan, diagnostics::Error>
plan_transitions(std::span<const ShotTiming> shots,
                 std::span<const TransitionIntent> intents) {
  if (shots.empty()) {
    return std::unexpected(
        make_error("empty_shots", "Transition planner requires packed shots"));
  }

  std::unordered_map<std::string, ShotRef> by_id;
  by_id.reserve(shots.size());
  std::uint64_t cursor = 0;
  for (std::uint32_t i = 0; i < shots.size(); ++i) {
    const ShotTiming &shot = shots[i];
    if (shot.id.empty()) {
      return std::unexpected(
          make_error("invalid_shot_id", "Shot id must be non-empty"));
    }
    if (shot.duration_frames == 0) {
      return std::unexpected(
          make_error("invalid_shot_duration",
                     "Shot duration_frames must be greater than 0"));
    }
    if (shot.start_frame != cursor) {
      return std::unexpected(make_error(
          "unpacked_shot_timeline",
          "Shots must form a contiguous packed timeline from frame 0"));
    }
    if (cursor >
        (std::numeric_limits<std::uint64_t>::max() - shot.duration_frames)) {
      return std::unexpected(
          make_error("duration_overflow", "Film duration would overflow"));
    }
    if (!by_id.emplace(shot.id, ShotRef{.index = i, .timing = &shot}).second) {
      return std::unexpected(
          make_error("duplicate_shot_id", "Shot ids must be unique"));
    }
    cursor += shot.duration_frames;
  }

  const std::uint64_t film_duration = cursor;
  TransitionPlan plan;
  plan.film_duration_frames = film_duration;
  plan.spans.reserve(intents.size());

  for (const TransitionIntent &intent : intents) {
    if (intent.id.empty()) {
      return std::unexpected(make_error("invalid_transition_id",
                                        "Transition id must be non-empty"));
    }
    const auto from_it = by_id.find(intent.from_shot_id);
    const auto to_it = by_id.find(intent.to_shot_id);
    if (from_it == by_id.end() || to_it == by_id.end()) {
      return std::unexpected(make_error(
          "unknown_transition_shot",
          "Transition references a shot that is not on the packed timeline"));
    }
    const ShotRef &from = from_it->second;
    const ShotRef &to = to_it->second;
    if (to.index != from.index + 1U) {
      return std::unexpected(
          make_error("non_adjacent_transition",
                     "Transitions must connect consecutive packed shots"));
    }

    const std::uint64_t boundary = to.timing->start_frame;
    PlannedTransition span;
    span.id = intent.id;
    span.from_shot_id = intent.from_shot_id;
    span.to_shot_id = intent.to_shot_id;
    span.from_shot_index = from.index;
    span.to_shot_index = to.index;
    span.kind = intent.kind;
    span.blend = blend_for(intent.kind);
    // Later shots already receive higher node layer_order in the compiler;
    // record the contract explicitly so overlap compositing stays ordered.
    span.from_layer_order = static_cast<std::int32_t>(from.index);
    span.to_layer_order = static_cast<std::int32_t>(to.index);
    if (!(span.to_layer_order > span.from_layer_order)) {
      return std::unexpected(
          make_error("invalid_layer_order",
                     "Incoming shot must paint above the outgoing shot"));
    }

    if (is_cut(intent.kind)) {
      if (intent.overlap_frames.has_value() && *intent.overlap_frames != 0) {
        return std::unexpected(
            make_error("cut_requires_zero_overlap",
                       "Hard/match cuts must not request overlap frames"));
      }
      span.start_frame = boundary;
      span.duration_frames = 0;
    } else {
      const std::uint64_t requested = intent.overlap_frames.value_or(0);
      if (requested == 0) {
        return std::unexpected(
            make_error("overlap_requires_positive_duration",
                       "Wipe/reveal transitions require overlap_frames > 0"));
      }
      // Keep the span inside both shots and straddling the boundary without
      // inventing frames beyond the packed film duration.
      const std::uint64_t max_half =
          std::min(from.timing->duration_frames, to.timing->duration_frames);
      if (max_half == 0) {
        return std::unexpected(
            make_error("unsatisfiable_overlap",
                       "Shots are too short to host an overlap transition"));
      }
      const std::uint64_t max_overlap = max_half * 2;
      if (requested > max_overlap) {
        return std::unexpected(
            make_error("overlap_exceeds_shots",
                       "Overlap does not fit inside the from/to shot ranges"));
      }
      std::uint64_t before = requested / 2;
      std::uint64_t after = requested - before;
      if (before > from.timing->duration_frames) {
        before = from.timing->duration_frames;
        after = requested - before;
      }
      if (after > to.timing->duration_frames) {
        after = to.timing->duration_frames;
        before = requested - after;
      }
      if (before == 0 || after == 0 || before > from.timing->duration_frames ||
          after > to.timing->duration_frames) {
        return std::unexpected(
            make_error("overlap_misses_boundary",
                       "Overlap transition must straddle the shot boundary"));
      }
      span.start_frame = boundary - before;
      span.duration_frames = requested;
      if (span.start_frame + span.duration_frames > film_duration) {
        return std::unexpected(
            make_error("transition_extends_duration",
                       "Transition must not extend the packed film duration"));
      }
    }

    plan.spans.push_back(std::move(span));
  }

  // Invariant: planning never rewrites the packed film length.
  if (plan.film_duration_frames != film_duration) {
    return std::unexpected(make_error(
        "duration_rewrite_detected",
        "Transition planner must preserve packed film durationFrames"));
  }
  return plan;
}

} // namespace instantee::direction
