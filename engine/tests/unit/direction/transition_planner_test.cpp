#include <instantee/direction/transition_planner.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/shot_timeline.hpp>
#include <instantee/runtime/transition_schedule.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using instantee::core::FrameIndex;
using instantee::direction::plan_transitions;
using instantee::direction::ShotTiming;
using instantee::direction::TransitionIntent;
using instantee::direction::TransitionKind;
using instantee::runtime::make_shot_timeline;
using instantee::runtime::make_transition_schedule;
using instantee::runtime::RuntimeShot;
using instantee::runtime::TransitionBlend;
using instantee::runtime::TransitionSpanDesc;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] TransitionBlend parse_blend(const std::string &blend) {
  if (blend == "cut") {
    return TransitionBlend::cut;
  }
  if (blend == "wipe") {
    return TransitionBlend::wipe;
  }
  return TransitionBlend::dissolve;
}

} // namespace

int main() {
  std::vector<ShotTiming> shots{
      {.id = "intro", .start_frame = 0, .duration_frames = 100},
      {.id = "outro", .start_frame = 100, .duration_frames = 30},
  };
  const std::uint64_t packed = 130;

  // Hard cut and match cut: zero duration at boundary, no length bump.
  {
    std::vector<TransitionIntent> intents{
        {.id = "hc",
         .from_shot_id = "intro",
         .to_shot_id = "outro",
         .kind = TransitionKind::hard_cut,
         .overlap_frames = 0},
    };
    auto plan = plan_transitions(shots, intents);
    expect(plan.has_value(), "hard cut plans");
    if (plan) {
      expect(plan->film_duration_frames == packed,
             "hard cut preserves duration");
      expect(plan->spans.size() == 1 && plan->spans[0].duration_frames == 0 &&
                 plan->spans[0].start_frame == 100 &&
                 plan->spans[0].blend == "cut",
             "hard cut span");
      expect(plan->spans[0].to_layer_order > plan->spans[0].from_layer_order,
             "hard cut layer order");
    }

    intents[0].id = "mc";
    intents[0].kind = TransitionKind::match_cut;
    auto match = plan_transitions(shots, intents);
    expect(match.has_value() && match->spans[0].blend == "cut" &&
               match->film_duration_frames == packed,
           "match cut preserves duration");
  }

  // Reveal (dissolve) and wipe: straddle boundary, no double-count.
  {
    std::vector<TransitionIntent> intents{
        {.id = "rev",
         .from_shot_id = "intro",
         .to_shot_id = "outro",
         .kind = TransitionKind::reveal,
         .overlap_frames = 20},
    };
    auto reveal = plan_transitions(shots, intents);
    expect(reveal.has_value(), "reveal plans");
    if (reveal) {
      expect(reveal->film_duration_frames == packed,
             "reveal preserves duration");
      expect(reveal->spans[0].blend == "dissolve", "reveal → dissolve");
      expect(reveal->spans[0].start_frame == 90 &&
                 reveal->spans[0].duration_frames == 20,
             "reveal straddles boundary");
      expect(reveal->spans[0].to_layer_order >
                 reveal->spans[0].from_layer_order,
             "reveal layer order");
    }

    intents[0].id = "wp";
    intents[0].kind = TransitionKind::wipe;
    auto wipe = plan_transitions(shots, intents);
    expect(wipe.has_value() && wipe->spans[0].blend == "wipe" &&
               wipe->film_duration_frames == packed,
           "wipe preserves duration");
  }

  // Runtime schedule agrees: output length unchanged, every frame once.
  {
    auto planned = plan_transitions(
        shots, std::vector<TransitionIntent>{{.id = "d1",
                                              .from_shot_id = "intro",
                                              .to_shot_id = "outro",
                                              .kind = TransitionKind::reveal,
                                              .overlap_frames = 20}});
    expect(planned.has_value(), "schedule source plan");
    if (planned) {
      std::vector<RuntimeShot> runtime_shots(2);
      runtime_shots[0].id = "intro";
      runtime_shots[0].start_frame = 0;
      runtime_shots[0].duration_frames = 100;
      runtime_shots[1].id = "outro";
      runtime_shots[1].start_frame = 100;
      runtime_shots[1].duration_frames = 30;
      auto timeline = make_shot_timeline(runtime_shots, FrameIndex{packed});
      expect(timeline.has_value(), "timeline");
      if (timeline) {
        TransitionSpanDesc desc;
        desc.id = planned->spans[0].id;
        desc.from_shot_index = planned->spans[0].from_shot_index;
        desc.to_shot_index = planned->spans[0].to_shot_index;
        desc.start = FrameIndex{planned->spans[0].start_frame};
        desc.duration_frames = planned->spans[0].duration_frames;
        desc.blend = parse_blend(planned->spans[0].blend);
        auto schedule =
            make_transition_schedule(*timeline, std::span{&desc, 1});
        expect(schedule.has_value(), "runtime accepts planned span");
        if (schedule) {
          expect(schedule->output_duration() == FrameIndex{packed},
                 "no double-counted frames");
        }
      }
    }
  }

  // Reject overlap that would extend duration / miss boundary / bad layer.
  {
    expect(
        !plan_transitions(
             shots, std::vector<TransitionIntent>{{.id = "bad",
                                                   .from_shot_id = "intro",
                                                   .to_shot_id = "outro",
                                                   .kind = TransitionKind::wipe,
                                                   .overlap_frames = 0}})
             .has_value(),
        "wipe requires overlap");
    expect(!plan_transitions(
                shots,
                std::vector<TransitionIntent>{{.id = "big",
                                               .from_shot_id = "intro",
                                               .to_shot_id = "outro",
                                               .kind = TransitionKind::reveal,
                                               .overlap_frames = 80}})
                .has_value(),
           "overlap exceeding short shot rejected");
    expect(!plan_transitions(shots,
                             std::vector<TransitionIntent>{
                                 {.id = "skip",
                                  .from_shot_id = "outro",
                                  .to_shot_id = "intro",
                                  .kind = TransitionKind::hard_cut}})
                .has_value(),
           "non-adjacent rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
