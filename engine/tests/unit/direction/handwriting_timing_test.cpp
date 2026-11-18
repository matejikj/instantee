#include <instantee/direction/editorial_beats.hpp>
#include <instantee/direction/handwriting.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace instantee::direction;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] const HandwritingStrokeSample *
find_stroke(const std::vector<HandwritingStrokeSample> &samples,
            std::string_view id) {
  for (const auto &s : samples) {
    if (s.stroke_id == id) {
      return &s;
    }
  }
  return nullptr;
}

} // namespace

int main() {
  const std::vector<HandwritingLineSpec> lines{
      {.stroke_id = "line_a", .weight = 1.0},
      {.stroke_id = "line_b", .weight = 1.0},
      {.stroke_id = "line_c", .weight = 2.0},
  };

  // Anchor to an editorial evidence beat and require exact beat coverage.
  EditorialBeatScheduleRequest req;
  req.total_duration_frames = 200;
  for (auto kind :
       {EditorialBeatKind::hook, EditorialBeatKind::claim,
        EditorialBeatKind::evidence, EditorialBeatKind::conclusion}) {
    EditorialBeatMarker m;
    m.kind = kind;
    m.weight = 1;
    req.beats.push_back(std::move(m));
  }
  auto schedule = schedule_editorial_beats(req);
  expect(schedule.has_value() && schedule->beats.size() == 4, "beat schedule");
  if (!schedule) {
    return EXIT_FAILURE;
  }
  const auto &evidence = schedule->beats[2];

  auto program = compile_handwriting_for_beat(
      evidence, HandwritingPreset::line_by_line, MotionEase::linear, lines,
      /*pause_fraction=*/0.1);
  expect(program.has_value(), "compile on evidence beat");
  if (!program) {
    return EXIT_FAILURE;
  }
  expect(program->lines.size() == 3, "three line windows");
  expect(program->lines.front().start_frame == evidence.start_frame,
         "first line starts on beat");
  expect(program->beat_end_frame ==
             evidence.start_frame + evidence.duration_frames,
         "program ends on beat end");
  {
    const auto &last = program->lines.back();
    expect(last.start_frame + last.duration_frames <= program->beat_end_frame,
           "last line inside beat");
  }

  // Before beat: all zero. After beat: all settled at 1.
  auto before = sample_handwriting_motion(*program, evidence.start_frame);
  expect(before.has_value(), "sample beat start");
  if (before) {
    for (const auto &s : *before) {
      expect(s.progress == 0.0, "zero at beat start");
    }
  }
  auto after = sample_handwriting_motion(*program, program->beat_end_frame);
  expect(after.has_value(), "sample beat end");
  if (after) {
    for (const auto &s : *after) {
      expect(std::fabs(s.progress - 1.0) < 1e-9, "settled at beat end");
    }
  }

  // During first line window, later lines stay at 0 (pause + sequential pack).
  {
    const auto &first = program->lines[0];
    const auto mid = first.start_frame + first.duration_frames / 2;
    auto mid_s = sample_handwriting_motion(*program, mid);
    expect(mid_s.has_value(), "sample mid first");
    if (mid_s) {
      const auto *a = find_stroke(*mid_s, "line_a");
      const auto *b = find_stroke(*mid_s, "line_b");
      expect(a && a->progress > 0.0 && a->progress < 1.0, "line_a drawing");
      expect(b && b->progress == 0.0, "line_b waits");
    }
  }

  // Lengthen the claim beat → evidence start shifts; handwriting must follow.
  auto resized = rescale_editorial_beat(
      *schedule, 1, schedule->beats[1].duration_frames + 20);
  expect(resized.has_value(), "rescale claim");
  if (resized) {
    const auto &new_ev = resized->beats[2];
    expect(new_ev.start_frame != evidence.start_frame, "evidence marker moved");
    auto prog2 =
        compile_handwriting_for_beat(new_ev, HandwritingPreset::line_by_line,
                                     MotionEase::linear, lines, 0.1);
    expect(prog2.has_value(), "recompile after rescale");
    if (prog2) {
      expect(prog2->lines.front().start_frame == new_ev.start_frame,
             "tracks follow new beat start");
      expect(prog2->beat_end_frame ==
                 new_ev.start_frame + new_ev.duration_frames,
             "tracks follow new beat end");
      expect(prog2->lines.front().start_frame !=
                 program->lines.front().start_frame,
             "absolute timing shifted with marker");
    }
  }

  // Dashed sketch: mid-progress has a lift gap somewhere in the window.
  {
    HandwritingMotionParams dash_params;
    dash_params.preset = HandwritingPreset::dashed_sketch;
    dash_params.ease = MotionEase::linear;
    dash_params.beat_start_frame = 0;
    dash_params.beat_duration_frames = 40;
    dash_params.pause_fraction = 0.0;
    dash_params.dash_on_fraction = 0.4;
    const std::vector<HandwritingLineSpec> one{
        {.stroke_id = "dash", .weight = 1}};
    auto dash = compile_handwriting_motion(dash_params, one);
    expect(dash.has_value(), "compile dash");
    if (dash) {
      bool saw_on = false;
      bool saw_off = false;
      for (std::uint64_t f = 1; f < 40; ++f) {
        auto s = sample_handwriting_motion(*dash, f);
        expect(s.has_value() && s->size() == 1, "dash sample");
        if (!s) {
          break;
        }
        if ((*s)[0].progress > 0.0 && (*s)[0].progress < 1.0) {
          if ((*s)[0].dash_visible) {
            saw_on = true;
          } else {
            saw_off = true;
          }
        }
      }
      expect(saw_on && saw_off, "dashed has on and off phases");
    }
  }

  // Correction scribble overshoots then settles to 1 inside the beat.
  {
    HandwritingMotionParams scrib;
    scrib.preset = HandwritingPreset::correction_scribble;
    scrib.ease = MotionEase::linear;
    scrib.beat_start_frame = 10;
    scrib.beat_duration_frames = 20;
    scrib.pause_fraction = 0.0;
    scrib.scribble_overshoot = 0.2;
    const std::vector<HandwritingLineSpec> one{
        {.stroke_id = "fix", .weight = 1}};
    auto prog = compile_handwriting_motion(scrib, one);
    expect(prog.has_value(), "compile scribble");
    if (prog) {
      auto mid = sample_handwriting_motion(*prog, 10 + 10);
      auto end = sample_handwriting_motion(*prog, 30);
      expect(mid && (*mid)[0].progress > 1.0, "scribble overshoots mid");
      expect(end && std::fabs((*end)[0].progress - 1.0) < 1e-9,
             "scribble settles");
      auto tracks = lower_handwriting_tracks(*prog, "shot_a");
      expect(tracks.has_value() && tracks->size() == 1, "lower scribble track");
      if (tracks) {
        expect((*tracks)[0].property == "ink_progress", "property");
        expect((*tracks)[0].target_id == "fix", "target");
        bool saw_peak = false;
        for (const auto &k : (*tracks)[0].keyframes) {
          if (k.value > 1.0) {
            saw_peak = true;
          }
        }
        expect(saw_peak, "track encodes overshoot keyframe");
      }
    }
  }

  // Reject empty / bad inputs.
  {
    HandwritingMotionParams bad;
    bad.beat_duration_frames = 0;
    expect(!validate_handwriting_params(bad).has_value(), "reject empty beat");
    HandwritingMotionParams ok;
    ok.beat_duration_frames = 10;
    expect(!compile_handwriting_motion(ok, {}).has_value(), "reject no lines");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "handwriting_timing_test: ok\n";
  return EXIT_SUCCESS;
}
