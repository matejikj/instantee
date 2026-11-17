#include <instantee/direction/editorial_beats.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
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

[[nodiscard]] EditorialBeatScheduleRequest
make_request(std::uint64_t total,
             std::initializer_list<std::pair<EditorialBeatKind, std::uint64_t>>
                 kind_weights) {
  EditorialBeatScheduleRequest req;
  req.total_duration_frames = total;
  std::size_t i = 0;
  for (const auto &[kind, weight] : kind_weights) {
    EditorialBeatMarker m;
    m.kind = kind;
    m.weight = weight;
    m.track_id = "beat_" + std::to_string(i);
    m.target_id = "node_" + std::to_string(i);
    m.property = "opacity";
    req.beats.push_back(std::move(m));
    ++i;
  }
  return req;
}

[[nodiscard]] std::uint64_t sum_durations(const EditorialBeatSchedule &s) {
  std::uint64_t sum = 0;
  for (const auto &b : s.beats) {
    sum += b.duration_frames;
  }
  return sum;
}

} // namespace

int main() {
  // Pack four editorial beats into a fixed budget.
  {
    auto req = make_request(120, {{EditorialBeatKind::hook, 2},
                                  {EditorialBeatKind::claim, 3},
                                  {EditorialBeatKind::evidence, 4},
                                  {EditorialBeatKind::conclusion, 1}});
    auto schedule = schedule_editorial_beats(req);
    expect(schedule.has_value(), "schedule packs");
    if (schedule) {
      expect(schedule->total_duration_frames == 120, "total");
      expect(sum_durations(*schedule) == 120, "sum");
      expect(schedule->beats.size() == 4, "four beats");
      expect(schedule->beats[0].kind == EditorialBeatKind::hook &&
                 schedule->beats[0].start_frame == 0,
             "hook at 0");
      expect(schedule->beats[1].start_frame ==
                 schedule->beats[0].duration_frames,
             "claim abuts hook");
      expect(schedule->tracks.size() == 4, "tracks for each beat");
      // Track offsets land on beat starts.
      auto off = editorial_beat_track_offset(*schedule, 2, 0);
      expect(off.has_value() && *off == schedule->beats[2].start_frame,
             "evidence offset");
    }
  }

  // Changing a beat duration reschedules subsequent events only; total fixed.
  {
    auto req = make_request(100, {{EditorialBeatKind::hook, 1},
                                  {EditorialBeatKind::claim, 1},
                                  {EditorialBeatKind::evidence, 1},
                                  {EditorialBeatKind::conclusion, 1}});
    auto base = schedule_editorial_beats(req);
    expect(base.has_value(), "base schedule");
    if (base) {
      const std::uint64_t old_claim = base->beats[1].duration_frames;
      const std::uint64_t old_evidence_start = base->beats[2].start_frame;
      const std::uint64_t old_conclusion_start = base->beats[3].start_frame;

      // Lengthen claim; evidence+conclusion must move and shrink/grow together.
      const std::uint64_t new_claim = old_claim + 10;
      auto rescaled = rescale_editorial_beat(*base, 1, new_claim);
      expect(rescaled.has_value(), "rescale claim");
      if (rescaled) {
        expect(rescaled->total_duration_frames == 100, "total unchanged");
        expect(sum_durations(*rescaled) == 100, "sum unchanged");
        expect(rescaled->beats[0].duration_frames ==
                   base->beats[0].duration_frames,
               "prefix hook unchanged");
        expect(rescaled->beats[0].start_frame == 0, "hook still 0");
        expect(rescaled->beats[1].duration_frames == new_claim, "claim new");
        expect(rescaled->beats[2].start_frame ==
                   rescaled->beats[0].duration_frames + new_claim,
               "evidence start recalculated");
        expect(rescaled->beats[2].start_frame != old_evidence_start ||
                   rescaled->beats[3].start_frame != old_conclusion_start,
               "subsequent moved");
        expect(rescaled->beats[3].start_frame +
                       rescaled->beats[3].duration_frames ==
                   100,
               "conclusion ends at total");

        // Track keyframes follow new offsets.
        expect(rescaled->tracks.size() == 4, "tracks rebuilt");
        expect(rescaled->tracks[2].keyframes.front().frame ==
                       rescaled->beats[2].start_frame ||
                   rescaled->tracks[2].keyframes[1].frame ==
                       rescaled->beats[2].start_frame,
               "evidence track offset");
      }

      // Shorten claim instead.
      auto shorter = rescale_editorial_beat(*base, 1, 5);
      expect(shorter.has_value() && shorter->total_duration_frames == 100 &&
                 shorter->beats[1].duration_frames == 5,
             "shorten claim");
    }
  }

  // Reject rescale of the last beat (no subsequent events).
  {
    auto req = make_request(
        40, {{EditorialBeatKind::hook, 1}, {EditorialBeatKind::conclusion, 1}});
    auto base = schedule_editorial_beats(req);
    expect(base.has_value(), "two-beat schedule");
    if (base) {
      expect(!rescale_editorial_beat(*base, 1, 10).has_value(),
             "reject last-beat rescale");
    }
  }

  // Reject durations that leave no room for subsequent beats.
  {
    auto req = make_request(40, {{EditorialBeatKind::hook, 1},
                                 {EditorialBeatKind::claim, 1},
                                 {EditorialBeatKind::evidence, 1}});
    auto base = schedule_editorial_beats(req);
    expect(base.has_value(), "three-beat");
    if (base) {
      expect(!rescale_editorial_beat(*base, 0, 39).has_value(),
             "reject starving subsequent");
    }
  }

  // Reject empty / zero total.
  {
    EditorialBeatScheduleRequest empty;
    empty.total_duration_frames = 10;
    expect(!schedule_editorial_beats(empty).has_value(), "reject empty");
    auto req = make_request(0, {{EditorialBeatKind::hook, 1}});
    expect(!schedule_editorial_beats(req).has_value(), "reject zero total");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
