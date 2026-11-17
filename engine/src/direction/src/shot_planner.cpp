#include <instantee/direction/shot_planner.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "shot_planner", message};
}

[[nodiscard]] bool intent_uses_hold(IntentKind intent) noexcept {
  switch (intent) {
  case IntentKind::reveal:
  case IntentKind::focus:
  case IntentKind::build_suspense:
    return true;
  case IntentKind::compare:
  case IntentKind::explain:
    return false;
  }
  return false;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_request(const ShotPlanRequest &request) {
  if (request.shot_id.empty()) {
    return std::unexpected(
        make_error("invalid_shot_id", "Shot id must be non-empty"));
  }
  if (request.author_duration_frames == 0) {
    return std::unexpected(make_error("invalid_shot_duration",
                                      "Author duration_frames must be greater "
                                      "than 0 and is never rewritten"));
  }
  if (request.intent.targets.empty()) {
    return std::unexpected(make_error(
        "insufficient_intent_targets",
        "Shot planner requires a validated semantic intent with targets"));
  }
  if (!(request.intent.intensity >= 0.0 && request.intent.intensity <= 1.0) ||
      !(request.intent.importance >= 0.0 && request.intent.importance <= 1.0)) {
    return std::unexpected(
        make_error("invalid_intent_parameters",
                   "Intent intensity/importance must be within [0, 1]"));
  }
  return {};
}

// Partition the immutable author budget into abutting subevents. Lengths always
// sum to `budget`; short shots collapse to a single action beat.
[[nodiscard]] std::vector<ShotSubevent>
partition_subevents(std::uint64_t budget, IntentKind intent) {
  std::vector<ShotSubevent> subevents;
  if (budget <= 2) {
    subevents.push_back(ShotSubevent{.kind = ShotSubeventKind::action,
                                     .start_frame = 0,
                                     .duration_frames = budget});
    return subevents;
  }

  const bool with_hold = intent_uses_hold(intent) && budget >= 4;
  // Proportional slices with integer remainders assigned left-to-right so the
  // sum is exact and the schedule is deterministic for a given budget/intent.
  const std::uint64_t establish = std::max<std::uint64_t>(1, budget / 6);
  const std::uint64_t resolve = std::max<std::uint64_t>(1, budget / 6);
  std::uint64_t hold = 0;
  if (with_hold) {
    hold = std::max<std::uint64_t>(1, budget / 6);
  }
  if (establish + resolve + hold >= budget) {
    // Pathological tiny budgets after the >=4 gate should not happen; fall
    // back to a single action covering the whole shot.
    subevents.push_back(ShotSubevent{.kind = ShotSubeventKind::action,
                                     .start_frame = 0,
                                     .duration_frames = budget});
    return subevents;
  }
  const std::uint64_t action = budget - establish - resolve - hold;

  std::uint64_t cursor = 0;
  auto push = [&](ShotSubeventKind kind, std::uint64_t length) {
    if (length == 0) {
      return;
    }
    subevents.push_back(ShotSubevent{
        .kind = kind, .start_frame = cursor, .duration_frames = length});
    cursor += length;
  };
  push(ShotSubeventKind::establish, establish);
  push(ShotSubeventKind::action, action);
  push(ShotSubeventKind::hold, hold);
  push(ShotSubeventKind::resolve, resolve);
  return subevents;
}

[[nodiscard]] bool
subevents_cover_budget(std::span<const ShotSubevent> subevents,
                       std::uint64_t budget) noexcept {
  if (subevents.empty()) {
    return false;
  }
  std::uint64_t sum = 0;
  std::uint64_t expected_start = 0;
  for (const ShotSubevent &ev : subevents) {
    if (ev.duration_frames == 0) {
      return false;
    }
    if (ev.start_frame != expected_start) {
      return false;
    }
    if (ev.start_frame > budget ||
        ev.duration_frames > budget - ev.start_frame) {
      return false;
    }
    sum += ev.duration_frames;
    expected_start = ev.start_frame + ev.duration_frames;
  }
  return sum == budget && expected_start == budget;
}

} // namespace

std::expected<ShotPlan, diagnostics::Error>
plan_shot(const ShotPlanRequest &request) {
  if (auto ok = validate_request(request); !ok) {
    return std::unexpected(ok.error());
  }

  ShotPlan plan;
  plan.shot_id = request.shot_id;
  // Author duration is the sole budget — never inflate/shrink here.
  plan.duration_frames = request.author_duration_frames;
  plan.start_frame = 0;
  plan.intent = request.intent.kind;
  plan.framing = request.camera.has_value()
                     ? request.camera->framing
                     : default_framing_for_intent(request.intent.kind);
  plan.subevents =
      partition_subevents(plan.duration_frames, request.intent.kind);

  if (!subevents_cover_budget(plan.subevents, plan.duration_frames)) {
    return std::unexpected(
        make_error("invalid_shot_timeline",
                   "Shot subevents must cover the author duration exactly"));
  }
  return plan;
}

std::expected<FilmShotPlan, diagnostics::Error>
plan_film_shots(std::span<const ShotPlanRequest> requests) {
  if (requests.empty()) {
    return std::unexpected(
        make_error("empty_film", "Film shot plan requires at least one shot"));
  }

  FilmShotPlan film;
  film.shots.reserve(requests.size());
  std::uint64_t cursor = 0;
  for (const ShotPlanRequest &request : requests) {
    auto planned = plan_shot(request);
    if (!planned) {
      return std::unexpected(planned.error());
    }
    if (cursor > (std::numeric_limits<std::uint64_t>::max() -
                  planned->duration_frames)) {
      return std::unexpected(make_error(
          "duration_overflow",
          "Film duration would overflow aggregating author shot budgets"));
    }
    planned->start_frame = cursor;
    cursor += planned->duration_frames;
    film.shots.push_back(std::move(*planned));
  }
  film.duration_frames = cursor;

  // Global duration is exactly the sum of author budgets — never rewritten by
  // intent packing, framing choice, or subevent scheduling.
  std::uint64_t author_sum = 0;
  for (const ShotPlanRequest &request : requests) {
    author_sum += request.author_duration_frames;
  }
  if (film.duration_frames != author_sum) {
    return std::unexpected(make_error(
        "duration_rewrite_detected",
        "Shot planner must preserve the author film durationFrames"));
  }
  return film;
}

} // namespace instantee::direction
