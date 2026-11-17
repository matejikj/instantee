#include <instantee/runtime/shot_timeline.hpp>

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

} // namespace

int main() {
  using namespace instantee::core;
  using namespace instantee::runtime;

  // Contiguous shots: [0,10) + [10,25) cover duration 25 with no gap/overlap.
  std::vector<RuntimeShot> shots(2);
  shots[0].id = "intro";
  shots[0].start_frame = 0;
  shots[0].duration_frames = 10;
  shots[1].id = "body";
  shots[1].start_frame = 10;
  shots[1].duration_frames = 15;

  auto timeline = make_shot_timeline(shots, FrameIndex{25});
  expect(timeline.has_value(), "make contiguous timeline");
  if (!timeline) {
    return EXIT_FAILURE;
  }
  expect(timeline->intervals().size() == 2, "two intervals");
  expect(timeline->intervals()[0].end_exclusive == FrameIndex{10},
         "first end-exclusive");
  expect(timeline->intervals()[1].end_exclusive == FrameIndex{25},
         "second end-exclusive");

  // Boundary fixtures: last frame of shot A / first of shot B.
  {
    auto last_a = timeline->resolve_global(FrameIndex{9});
    auto first_b = timeline->resolve_global(FrameIndex{10});
    expect(last_a.has_value() && first_b.has_value(), "boundary resolve");
    if (last_a && first_b) {
      expect(last_a->shot_index == 0 && last_a->local == FrameIndex{9},
             "frame 9 is last of intro");
      expect(first_b->shot_index == 1 && first_b->local == FrameIndex{0},
             "frame 10 is first of body");
    }
    // No missing frame: every global in [0,25) resolves exactly once.
    std::vector<std::uint64_t> owned(25, 0);
    for (std::uint64_t g = 0; g < 25; ++g) {
      auto loc = timeline->resolve_global(FrameIndex{g});
      expect(loc.has_value(), "every film frame owned");
      if (loc) {
        ++owned[g];
        auto back = timeline->to_global(loc->shot_index, loc->local);
        expect(back.has_value() && *back == FrameIndex{g},
               "local↔global round-trip");
      }
    }
    for (std::uint64_t g = 0; g < 25; ++g) {
      expect(owned[g] == 1, "no missing or duplicate global frame");
    }
    // No extra frame beyond duration.
    expect(!timeline->resolve_global(FrameIndex{25}).has_value(),
           "reject frame == duration");
  }

  // Shot-local past end is rejected (no inventing an extra frame).
  expect(!timeline->to_global(0, FrameIndex{10}).has_value(),
         "reject local == duration");
  expect(timeline->to_global(1, FrameIndex{14}).has_value(),
         "accept last local of body");

  // Gap between shots rejected.
  {
    std::vector<RuntimeShot> gapped = shots;
    gapped[1].start_frame = 11;
    expect(!make_shot_timeline(gapped, FrameIndex{25}).has_value(),
           "reject gap");
  }

  // Duration mismatch (coverage short / long) rejected.
  expect(!make_shot_timeline(shots, FrameIndex{24}).has_value(),
         "reject short coverage");
  expect(!make_shot_timeline(shots, FrameIndex{26}).has_value(),
         "reject long declared duration");

  // Zero-length shot rejected.
  {
    std::vector<RuntimeShot> empty_shot = shots;
    empty_shot[0].duration_frames = 0;
    empty_shot[1].start_frame = 0;
    empty_shot[1].duration_frames = 25;
    expect(!make_shot_timeline(empty_shot, FrameIndex{25}).has_value(),
           "reject empty shot");
  }

  // Single-frame shot at the cut: [0,1) then [1,2).
  {
    std::vector<RuntimeShot> tiny(2);
    tiny[0].id = "a";
    tiny[0].start_frame = 0;
    tiny[0].duration_frames = 1;
    tiny[1].id = "b";
    tiny[1].start_frame = 1;
    tiny[1].duration_frames = 1;
    auto tl = make_shot_timeline(tiny, FrameIndex{2});
    expect(tl.has_value(), "single-frame shots");
    if (tl) {
      auto f0 = tl->resolve_global(FrameIndex{0});
      auto f1 = tl->resolve_global(FrameIndex{1});
      expect(f0 && f0->shot_index == 0 && f0->local == FrameIndex{0},
             "frame 0");
      expect(f1 && f1->shot_index == 1 && f1->local == FrameIndex{0},
             "frame 1");
      expect(!tl->resolve_global(FrameIndex{2}).has_value(), "no extra");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
