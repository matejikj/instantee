#include <instantee/direction/direction_report.hpp>
#include <instantee/direction/motion_grammar.hpp>
#include <instantee/direction/semantic_intent.hpp>
#include <instantee/direction/track_precedence.hpp>
#include <instantee/direction/transition_planner.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::ChannelDecision;
using instantee::direction::DirectedKeyframe;
using instantee::direction::DirectedTrack;
using instantee::direction::DirectionReport;
using instantee::direction::DirectionWarning;
using instantee::direction::GesturePattern;
using instantee::direction::IntentKind;
using instantee::direction::make_channel_decisions;
using instantee::direction::make_intent_decision;
using instantee::direction::make_semantic_intent;
using instantee::direction::make_transition_decision;
using instantee::direction::MotionGrammarBinding;
using instantee::direction::plan_transitions;
using instantee::direction::PlannedTransition;
using instantee::direction::resolve_track_precedence_detailed;
using instantee::direction::ShotTiming;
using instantee::direction::track_source_id;
using instantee::direction::TrackClaim;
using instantee::direction::TrackSource;
using instantee::direction::TransitionKind;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] DirectedTrack
make_track(std::string_view id, std::string_view target, std::string_view prop,
           std::initializer_list<DirectedKeyframe> kfs) {
  DirectedTrack track;
  track.id = std::string(id);
  track.target_id = std::string(target);
  track.property = std::string(prop);
  track.keyframes.assign(kfs);
  return track;
}

} // namespace

int main() {
  const std::array<std::string_view, 1> known{"headline"};
  const std::array<std::string_view, 1> targets{"headline"};
  auto intent = make_semantic_intent(IntentKind::reveal, targets, 0.8, 0.9,
                                     std::nullopt, known);
  expect(intent.has_value(), "semantic intent");
  if (!intent) {
    return EXIT_FAILURE;
  }

  MotionGrammarBinding binding{.package_id = "editorial",
                               .intent = IntentKind::reveal,
                               .pattern = GesturePattern::reveal,
                               .duration_scale = 0.5,
                               .intensity_scale = 1.0,
                               .property = "opacity",
                               .from_value = 0.0};

  // Build a report: intent + precedence winner + transition + warning.
  DirectionReport report;
  report.add_intent(make_intent_decision("intro", *intent, binding,
                                         TrackSource::style_bible));

  std::vector<TrackClaim> claims{
      {.track =
           make_track("cds_op", "headline", "opacity", {{0, 0.0}, {20, 0.8}}),
       .source = TrackSource::style_bible},
      {.track = make_track("author_op", "headline", "opacity",
                           {{0, 1.0}, {20, 1.0}}),
       .source = TrackSource::explicit_author},
  };
  // Author + CDS on same channel is a hard conflict in precedence — use
  // shot_override vs style to demonstrate a clean winner with audit source.
  claims[1].source = TrackSource::shot_override;
  claims[1].track.id = "override_op";
  claims[1].track.keyframes = {{0, 0.2}, {20, 1.0}};

  auto resolved = resolve_track_precedence_detailed(claims);
  expect(resolved.has_value(), "precedence resolves");
  if (!resolved) {
    return EXIT_FAILURE;
  }
  expect(resolved->winners.size() == 1 &&
             resolved->winners.front().source == TrackSource::shot_override,
         "shot override wins");
  for (auto &decision : make_channel_decisions("intro", resolved->winners)) {
    report.add_channel(std::move(decision));
  }

  std::vector<ShotTiming> shots{
      {.id = "intro", .start_frame = 0, .duration_frames = 100},
      {.id = "outro", .start_frame = 100, .duration_frames = 40},
  };
  auto planned = plan_transitions(
      shots, std::vector<instantee::direction::TransitionIntent>{
                 {.id = "cut1",
                  .from_shot_id = "intro",
                  .to_shot_id = "outro",
                  .kind = TransitionKind::hard_cut,
                  .overlap_frames = 0}});
  expect(planned.has_value() && !planned->spans.empty(), "transition planned");
  if (planned && !planned->spans.empty()) {
    report.add_transition(make_transition_decision(planned->spans.front()));
  }

  report.add_warning(DirectionWarning{
      .code = "soft_overlap_nudge",
      .message = "Composition nudged a low-importance deco",
      .shot_id = "intro",
      .object_id = "deco",
  });

  // Explain final choice for object/frame from the audit log.
  auto explanation = report.explain("headline", 10);
  expect(explanation.has_value(), "explain succeeds");
  if (explanation) {
    expect(explanation->object_id == "headline", "explain object");
    expect(explanation->shot_id == "intro", "explain shot");
    expect(explanation->property == "opacity", "explain property");
    expect(explanation->source == TrackSource::shot_override, "explain source");
    expect(explanation->reason_code == "shot_override_wins", "explain reason");
    expect(explanation->intent.has_value() &&
               *explanation->intent == IntentKind::reveal,
           "explain intent");
    expect(explanation->pattern.has_value() &&
               *explanation->pattern == GesturePattern::reveal,
           "explain pattern");
    expect(explanation->summary.find("source=shot_override") !=
               std::string::npos,
           "summary names source");
    expect(explanation->summary.find("reason=shot_override_wins") !=
               std::string::npos,
           "summary names reason");
    expect(explanation->value > 0.2 && explanation->value < 1.0,
           "interpolated value at frame 10");
  }

  // Deterministic audit snapshot (golden shape).
  const std::string snap_a = report.to_snapshot();
  const std::string snap_b = report.to_snapshot();
  expect(snap_a == snap_b, "snapshot deterministic");
  expect(snap_a.find("direction_report/1") == 0, "snapshot header");
  expect(snap_a.find("intent shot=intro") != std::string::npos, "intent line");
  expect(snap_a.find("pattern=reveal") != std::string::npos, "pattern logged");
  expect(snap_a.find("channel shot=intro object=headline property=opacity") !=
             std::string::npos,
         "channel line");
  expect(snap_a.find("source=shot_override") != std::string::npos,
         "override source logged");
  expect(snap_a.find("transition id=cut1") != std::string::npos,
         "transition logged");
  expect(snap_a.find("warning severity=warning code=soft_overlap_nudge") !=
             std::string::npos,
         "warning logged");

  // Missing object fails closed.
  expect(!report.explain("missing", 0).has_value(), "unknown object rejected");

  // Style-bible-only path remains explainable.
  {
    DirectionReport style_report;
    style_report.add_intent(
        make_intent_decision("s0", *intent, binding, TrackSource::style_bible));
    TrackClaim cds{
        .track = make_track("t0", "headline", "opacity", {{0, 0.0}, {15, 0.7}}),
        .source = TrackSource::style_bible};
    auto alone = resolve_track_precedence_detailed(std::span{&cds, 1});
    expect(alone.has_value(), "single claim resolves");
    if (alone) {
      for (auto &decision : make_channel_decisions("s0", alone->winners)) {
        style_report.add_channel(std::move(decision));
      }
      auto why = style_report.explain("headline", 0);
      expect(why.has_value() && why->source == TrackSource::style_bible &&
                 why->reason_code == "style_bible_wins",
             "style bible explanation");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
