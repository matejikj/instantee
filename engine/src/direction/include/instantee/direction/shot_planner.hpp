#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Timed beat inside a single shot. Frames are relative to the shot start and
// always lie in [0, shot.duration_frames). Subevent durations sum exactly to
// the author-supplied shot budget — the planner never rewrites duration.
enum class ShotSubeventKind : std::uint8_t {
  establish = 1,
  action,
  hold,
  resolve,
};

struct ShotSubevent {
  ShotSubeventKind kind = ShotSubeventKind::action;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
};

struct ShotPlanRequest {
  std::string shot_id;
  // Immutable author budget. Must be > 0.
  std::uint64_t author_duration_frames = 0;
  SemanticIntent intent{};
  // Optional Style Bible camera token; when absent, framing is derived from
  // the intent kind using the curated defaults below.
  std::optional<CameraToken> camera{};
};

struct ShotPlan {
  std::string shot_id;
  std::uint64_t duration_frames = 0;
  std::uint64_t start_frame = 0; // absolute film timeline offset
  CameraFraming framing = CameraFraming::static_framing;
  IntentKind intent = IntentKind::reveal;
  std::vector<ShotSubevent> subevents;
};

struct FilmShotPlan {
  std::uint64_t duration_frames = 0;
  std::vector<ShotPlan> shots;
};

// Plan framing + subevents for one shot without changing its duration budget.
[[nodiscard]] std::expected<ShotPlan, diagnostics::Error>
plan_shot(const ShotPlanRequest &request);

// Plan sequential shots. Film duration equals the sum of author budgets; shot
// starts abut without gaps or overlaps. Input order is preserved.
[[nodiscard]] std::expected<FilmShotPlan, diagnostics::Error>
plan_film_shots(std::span<const ShotPlanRequest> requests);

[[nodiscard]] constexpr std::string_view
shot_subevent_kind_id(ShotSubeventKind kind) noexcept {
  switch (kind) {
  case ShotSubeventKind::establish:
    return "establish";
  case ShotSubeventKind::action:
    return "action";
  case ShotSubeventKind::hold:
    return "hold";
  case ShotSubeventKind::resolve:
    return "resolve";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
camera_framing_id(CameraFraming framing) noexcept {
  switch (framing) {
  case CameraFraming::static_framing:
    return "static";
  case CameraFraming::push_in:
    return "push_in";
  case CameraFraming::pull_out:
    return "pull_out";
  case CameraFraming::pan:
    return "pan";
  case CameraFraming::orbit:
    return "orbit";
  }
  return "unknown";
}

// Curated intent → framing defaults when no Style Bible camera token is given.
[[nodiscard]] constexpr CameraFraming
default_framing_for_intent(IntentKind intent) noexcept {
  switch (intent) {
  case IntentKind::reveal:
    return CameraFraming::push_in;
  case IntentKind::focus:
    return CameraFraming::push_in;
  case IntentKind::compare:
    return CameraFraming::pan;
  case IntentKind::explain:
    return CameraFraming::static_framing;
  case IntentKind::build_suspense:
    return CameraFraming::orbit;
  }
  return CameraFraming::static_framing;
}

} // namespace instantee::direction
