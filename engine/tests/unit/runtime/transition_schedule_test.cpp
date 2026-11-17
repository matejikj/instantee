#include <instantee/runtime/transition_schedule.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_near(double actual, double expected, double tol,
                 const char *message) {
  if (!(std::isfinite(actual) && std::abs(actual - expected) <= tol)) {
    std::cerr << "FAIL: " << message << " got " << actual << " expected "
              << expected << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;

  // Packed shots matching the fixture shape: [0,100) + [100,130) = 130 frames.
  std::vector<RuntimeShot> shots(2);
  shots[0].id = "intro";
  shots[0].start_frame = 0;
  shots[0].duration_frames = 100;
  shots[1].id = "outro";
  shots[1].start_frame = 100;
  shots[1].duration_frames = 30;
  auto timeline = make_shot_timeline(shots, FrameIndex{130});
  expect(timeline.has_value(), "packed timeline");
  if (!timeline) {
    return EXIT_FAILURE;
  }

  // Dissolve straddling the cut: [90, 110) — 10 frames each side, no length
  // bump.
  TransitionSpanDesc dissolve;
  dissolve.id = "dissolve1";
  dissolve.from_shot_index = 0;
  dissolve.to_shot_index = 1;
  dissolve.start = FrameIndex{90};
  dissolve.duration_frames = 20;
  dissolve.blend = TransitionBlend::dissolve;

  auto schedule = make_transition_schedule(*timeline, std::span{&dissolve, 1});
  expect(schedule.has_value(), "make dissolve schedule");
  if (!schedule) {
    return EXIT_FAILURE;
  }
  expect(schedule->output_duration() == FrameIndex{130},
         "output duration equals packed film (no doubling)");
  expect(schedule->spans().size() == 1, "one span");
  expect(schedule->spans()[0].end_exclusive == FrameIndex{110},
         "dissolve end-exclusive");

  const FrameRate rate = FrameRate::media_30_1();

  // Monotonic PTS across the whole output; every frame scheduled once.
  RationalTime prev = RationalTime::zero();
  bool first = true;
  for (std::uint64_t f = 0; f < 130; ++f) {
    auto row = schedule->schedule_frame(FrameIndex{f}, rate);
    expect(row.has_value(), "schedule every output frame");
    if (!row) {
      break;
    }
    if (!first) {
      expect(row->pts > prev, "PTS strictly increasing");
    }
    first = false;
    prev = row->pts;

    if (f >= 90 && f < 110) {
      expect(row->mix.has_value(), "mix inside dissolve");
      if (row->mix) {
        expect(row->mix->from_shot_index == 0 && row->mix->to_shot_index == 1,
               "mix shots");
        expect_near(row->mix->mix_t, static_cast<double>(f - 90) / 20.0, 1e-12,
                    "mix_t");
        if (f < 100) {
          expect(row->mix->from_local == FrameIndex{f}, "from local pre-cut");
          expect(row->mix->to_local == FrameIndex{f - 90},
                 "to local pre-cut preview");
        } else {
          expect(row->mix->from_local == FrameIndex{99},
                 "from holds last pre-cut");
          expect(row->mix->to_local == FrameIndex{f - 100},
                 "to local post-cut");
        }
      }
    } else {
      expect(!row->mix.has_value(), "no mix outside dissolve");
    }
  }
  expect(!schedule->schedule_frame(FrameIndex{130}, rate).has_value(),
         "no extra frame beyond duration");

  // Instantaneous cut at the boundary.
  {
    TransitionSpanDesc cut;
    cut.id = "cut1";
    cut.from_shot_index = 0;
    cut.to_shot_index = 1;
    cut.start = FrameIndex{100};
    cut.duration_frames = 0;
    cut.blend = TransitionBlend::cut;
    auto cut_sched = make_transition_schedule(*timeline, std::span{&cut, 1});
    expect(cut_sched.has_value(), "cut schedule");
    if (cut_sched) {
      expect(cut_sched->output_duration() == FrameIndex{130},
             "cut keeps duration");
      auto before = cut_sched->schedule_frame(FrameIndex{99}, rate);
      auto after = cut_sched->schedule_frame(FrameIndex{100}, rate);
      expect(before && after, "cut boundary frames");
      if (before && after) {
        expect(before->primary_shot_index == 0, "before cut");
        expect(after->primary_shot_index == 1, "after cut");
        expect(!before->mix.has_value() && !after->mix.has_value(),
               "cut has no mix payload");
        expect(after->pts > before->pts, "PTS monotone across cut");
      }
    }
  }

  // Reject span that would extend the film.
  {
    TransitionSpanDesc long_span = dissolve;
    long_span.start = FrameIndex{120};
    long_span.duration_frames = 20; // ends at 140 > 130
    expect(!make_transition_schedule(*timeline, std::span{&long_span, 1})
                .has_value(),
           "reject duration extension");
  }

  // Reject overlap that misses the boundary / non-adjacent shots.
  {
    TransitionSpanDesc miss = dissolve;
    miss.start = FrameIndex{70};
    miss.duration_frames = 10; // [70,80) entirely in intro
    expect(
        !make_transition_schedule(*timeline, std::span{&miss, 1}).has_value(),
        "reject miss boundary");
    TransitionSpanDesc skip = dissolve;
    skip.to_shot_index = 0;
    expect(
        !make_transition_schedule(*timeline, std::span{&skip, 1}).has_value(),
        "reject non-adjacent");
  }

  // Cut with non-zero duration rejected.
  {
    TransitionSpanDesc bad_cut;
    bad_cut.id = "bad";
    bad_cut.from_shot_index = 0;
    bad_cut.to_shot_index = 1;
    bad_cut.start = FrameIndex{100};
    bad_cut.duration_frames = 5;
    bad_cut.blend = TransitionBlend::cut;
    expect(!make_transition_schedule(*timeline, std::span{&bad_cut, 1})
                .has_value(),
           "reject fat cut");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
