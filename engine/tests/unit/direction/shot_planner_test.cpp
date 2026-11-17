#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/shot_planner.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::CameraFraming;
using instantee::direction::CameraToken;
using instantee::direction::default_framing_for_intent;
using instantee::direction::FilmShotPlan;
using instantee::direction::IntentKind;
using instantee::direction::make_semantic_intent;
using instantee::direction::MotionEase;
using instantee::direction::plan_film_shots;
using instantee::direction::plan_shot;
using instantee::direction::shot_subevent_kind_id;
using instantee::direction::ShotPlanRequest;
using instantee::direction::ShotSubeventKind;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::uint64_t
sum_subevent_durations(const instantee::direction::ShotPlan &plan) {
  std::uint64_t sum = 0;
  for (const auto &ev : plan.subevents) {
    sum += ev.duration_frames;
  }
  return sum;
}

[[nodiscard]] bool
timeline_contiguous(const instantee::direction::ShotPlan &plan) {
  std::uint64_t cursor = 0;
  for (const auto &ev : plan.subevents) {
    if (ev.duration_frames == 0 || ev.start_frame != cursor) {
      return false;
    }
    cursor += ev.duration_frames;
  }
  return cursor == plan.duration_frames;
}

[[nodiscard]] ShotPlanRequest
make_request(std::string_view shot_id, std::uint64_t duration, IntentKind kind,
             std::optional<CameraToken> camera = std::nullopt) {
  const std::array<std::string_view, 2> known{"a", "b"};
  const std::array<std::string_view, 1> one{"a"};
  const std::array<std::string_view, 2> two{"a", "b"};
  const auto targets = (kind == IntentKind::compare)
                           ? std::span<const std::string_view>(two)
                           : std::span<const std::string_view>(one);
  auto intent =
      make_semantic_intent(kind, targets, 0.7, 0.5, std::nullopt, known);
  if (!intent) {
    std::cerr << "FAIL: make_semantic_intent " << intent.error().code << '\n';
    std::exit(EXIT_FAILURE);
  }
  ShotPlanRequest request;
  request.shot_id = std::string(shot_id);
  request.author_duration_frames = duration;
  request.intent = *intent;
  request.camera = camera;
  return request;
}

} // namespace

int main() {
  constexpr std::array<IntentKind, 5> all_intents{
      IntentKind::reveal, IntentKind::focus, IntentKind::compare,
      IntentKind::explain, IntentKind::build_suspense};

  // Every allowed intent preserves the author durationFrames exactly.
  for (IntentKind kind : all_intents) {
    for (std::uint64_t duration : {1u, 2u, 3u, 4u, 6u, 30u, 360u}) {
      auto request = make_request("shot", duration, kind);
      auto plan = plan_shot(request);
      expect(plan.has_value(), "plan_shot succeeds for allowed intent");
      if (!plan) {
        continue;
      }
      expect(plan->duration_frames == duration,
             "output frames equal author budget");
      expect(sum_subevent_durations(*plan) == duration,
             "subevents sum to author budget");
      expect(timeline_contiguous(*plan), "subevents abut without gaps");
      expect(plan->framing == default_framing_for_intent(kind),
             "default framing matches intent");
      expect(!plan->subevents.empty(), "at least one subevent");
      expect(plan->subevents.front().kind == ShotSubeventKind::action ||
                 plan->subevents.front().kind == ShotSubeventKind::establish,
             "timeline starts with establish or action");
    }
  }

  // Style Bible camera token overrides default framing without changing
  // duration.
  {
    CameraToken camera{.framing = CameraFraming::pull_out,
                       .ease = MotionEase::ease_in_out};
    auto request = make_request("cam", 48, IntentKind::explain, camera);
    auto plan = plan_shot(request);
    expect(plan.has_value(), "camera token plan");
    if (plan) {
      expect(plan->duration_frames == 48, "camera token preserves duration");
      expect(plan->framing == CameraFraming::pull_out,
             "camera token selects framing");
    }
  }

  // Film timeline: global duration equals sum of author budgets for mixed
  // intents; shot starts abut; no duration rewrite.
  {
    std::vector<ShotPlanRequest> requests;
    requests.push_back(make_request("s0", 90, IntentKind::reveal));
    requests.push_back(make_request("s1", 120, IntentKind::compare));
    requests.push_back(make_request("s2", 60, IntentKind::explain));
    requests.push_back(make_request("s3", 45, IntentKind::build_suspense));
    requests.push_back(make_request("s4", 30, IntentKind::focus));
    const std::uint64_t author_total = 90 + 120 + 60 + 45 + 30;

    auto film = plan_film_shots(requests);
    expect(film.has_value(), "film plan succeeds");
    if (film) {
      expect(film->duration_frames == author_total,
             "film duration equals author sum");
      expect(film->shots.size() == requests.size(), "all shots planned");
      std::uint64_t cursor = 0;
      for (std::size_t i = 0; i < film->shots.size(); ++i) {
        expect(film->shots[i].duration_frames ==
                   requests[i].author_duration_frames,
               "per-shot author duration preserved");
        expect(film->shots[i].start_frame == cursor, "shot starts abut");
        cursor += film->shots[i].duration_frames;
        expect(timeline_contiguous(film->shots[i]), "shot timeline contiguous");
      }
      expect(cursor == author_total, "cursor matches author total");
    }
  }

  // Determinism: identical requests → identical plans.
  {
    auto a = plan_shot(make_request("d0", 72, IntentKind::reveal));
    auto b = plan_shot(make_request("d0", 72, IntentKind::reveal));
    expect(a.has_value() && b.has_value(), "deterministic plans");
    if (a && b) {
      expect(a->duration_frames == b->duration_frames, "same duration");
      expect(a->framing == b->framing, "same framing");
      expect(a->subevents.size() == b->subevents.size(), "same beat count");
      if (a->subevents.size() == b->subevents.size()) {
        for (std::size_t i = 0; i < a->subevents.size(); ++i) {
          expect(a->subevents[i].kind == b->subevents[i].kind &&
                     a->subevents[i].start_frame ==
                         b->subevents[i].start_frame &&
                     a->subevents[i].duration_frames ==
                         b->subevents[i].duration_frames,
                 "same subevent schedule");
        }
      }
    }
  }

  // Reject zero duration and empty film — never invent a rewrite.
  {
    auto zero = make_request("z", 1, IntentKind::reveal);
    zero.author_duration_frames = 0;
    expect(!plan_shot(zero).has_value(), "zero duration rejected");
    expect(!plan_film_shots({}).has_value(), "empty film rejected");
  }

  // Subevent ids stay stable for diagnostics (smoke).
  expect(shot_subevent_kind_id(ShotSubeventKind::hold) == "hold", "hold id");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
