#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/directed_film.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Editorial narrative beats as pure schedule data. Markers occupy an immutable
// total duration; changing one beat's length reschedules subsequent beats and
// rebuilds track offsets without rewriting the film/shot budget.

enum class EditorialBeatKind : std::uint8_t {
  hook = 1,
  claim = 2,
  evidence = 3,
  conclusion = 4,
};

struct EditorialBeatMarker {
  EditorialBeatKind kind = EditorialBeatKind::hook;
  // Relative weight used when packing into the total budget. Must be > 0.
  std::uint64_t weight = 1;
  // Optional track binding; empty id skips track emission for this beat.
  std::string track_id{};
  std::string target_id{};
  // Property driven by the beat gate (defaults to opacity when empty).
  std::string property{};
};

struct EditorialBeatScheduleRequest {
  // Immutable author budget. Must be > 0 and cover every beat with >= 1 frame.
  std::uint64_t total_duration_frames = 0;
  std::vector<EditorialBeatMarker> beats{};
};

struct ScheduledEditorialBeat {
  EditorialBeatKind kind = EditorialBeatKind::hook;
  std::uint64_t start_frame = 0;
  std::uint64_t duration_frames = 0;
  std::string track_id{};
  std::string target_id{};
  std::string property{};
};

struct EditorialBeatSchedule {
  std::uint64_t total_duration_frames = 0;
  std::vector<ScheduledEditorialBeat> beats{};
  // One DirectedTrack per beat that declared track/target ids. Keyframes are
  // shot-local offsets: off before the beat, on across the beat window.
  std::vector<DirectedTrack> tracks{};
};

[[nodiscard]] constexpr std::string_view
editorial_beat_kind_id(EditorialBeatKind kind) noexcept {
  switch (kind) {
  case EditorialBeatKind::hook:
    return "hook";
  case EditorialBeatKind::claim:
    return "claim";
  case EditorialBeatKind::evidence:
    return "evidence";
  case EditorialBeatKind::conclusion:
    return "conclusion";
  }
  return "unknown";
}

// Pack markers into abutting beats whose durations sum exactly to the budget.
[[nodiscard]] std::expected<EditorialBeatSchedule, diagnostics::Error>
schedule_editorial_beats(const EditorialBeatScheduleRequest &request);

// Change beat `index` to `new_duration_frames` and proportionally reschedule
// all subsequent beats so the total duration is unchanged. Fails when there is
// no subsequent beat to absorb the delta, or when the new length cannot leave
// at least one frame for each following beat.
[[nodiscard]] std::expected<EditorialBeatSchedule, diagnostics::Error>
rescale_editorial_beat(const EditorialBeatSchedule &schedule, std::size_t index,
                       std::uint64_t new_duration_frames);

// Absolute film/shot offset for a beat-local frame (start + local).
[[nodiscard]] std::expected<std::uint64_t, diagnostics::Error>
editorial_beat_track_offset(const EditorialBeatSchedule &schedule,
                            std::size_t beat_index, std::uint64_t local_frame);

} // namespace instantee::direction
