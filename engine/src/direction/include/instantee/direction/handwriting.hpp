#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/directed_film.hpp>
#include <instantee/direction/editorial_beats.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Notebook handwriting motion modes. Timing is pure schedule data: progress
// tracks are anchored to a shot beat window so rescheduling a marker shifts
// every stroke without frame-loop scripting.
enum class HandwritingPreset : std::uint8_t {
  line_by_line = 1,        // sequential ink_progress 0→1 per line
  dashed_sketch = 2,       // same pack, with on/off dash gating
  correction_scribble = 3, // overshoot past 1.0 then settle to 1.0
};

struct HandwritingLineSpec {
  std::string stroke_id{}; // DirectedTrack target (ink stroke id)
  // Relative draw weight inside the beat; must be > 0.
  double weight = 1.0;
};

struct HandwritingMotionParams {
  HandwritingPreset preset = HandwritingPreset::line_by_line;
  MotionEase ease = MotionEase::ease_in_out;
  // Absolute shot-local beat window. Must leave room for every line + pauses.
  std::uint64_t beat_start_frame = 0;
  std::uint64_t beat_duration_frames = 0;
  // Pause between lines as a fraction of the beat, in [0, 0.5].
  double pause_fraction = 0.08;
  // Dashed sketch: fraction of each dash cycle that is "on", in (0, 1].
  double dash_on_fraction = 0.55;
  // Correction scribble peak beyond 1.0, in [0, 0.5].
  double scribble_overshoot = 0.15;
};

struct HandwritingLineWindow {
  std::string stroke_id{};
  std::uint64_t start_frame = 0; // absolute shot-local
  std::uint64_t duration_frames = 0;
};

struct HandwritingMotionProgram {
  HandwritingMotionParams params{};
  std::vector<HandwritingLineWindow> lines{};
  std::uint64_t beat_end_frame = 0; // start + duration
};

struct HandwritingStrokeSample {
  std::string stroke_id{};
  double progress = 0.0; // ink reveal; scribble may temporarily exceed 1.0
  // For dashed_sketch: false means the pen is lifted (gap). Other presets
  // keep this true whenever progress > 0.
  bool dash_visible = true;
};

[[nodiscard]] constexpr std::string_view
handwriting_preset_id(HandwritingPreset preset) noexcept {
  switch (preset) {
  case HandwritingPreset::line_by_line:
    return "line_by_line";
  case HandwritingPreset::dashed_sketch:
    return "dashed_sketch";
  case HandwritingPreset::correction_scribble:
    return "correction_scribble";
  }
  return "unknown";
}

[[nodiscard]] std::expected<HandwritingMotionParams, diagnostics::Error>
validate_handwriting_params(HandwritingMotionParams params);

// Map Style Bible motion token ids: handwriting_line, handwriting_dash,
// handwriting_scribble. Timing window still comes from the shot beat.
[[nodiscard]] std::expected<HandwritingMotionParams, diagnostics::Error>
make_handwriting_params(std::string_view preset_id, const MotionToken &token,
                        std::uint64_t beat_start_frame,
                        std::uint64_t beat_duration_frames);

// Pack line windows into the beat, inserting parameterized pauses. Durations
// sum (with pauses) exactly to the beat duration.
[[nodiscard]] std::expected<HandwritingMotionProgram, diagnostics::Error>
compile_handwriting_motion(const HandwritingMotionParams &params,
                           std::span<const HandwritingLineSpec> lines);

// Convenience: use a scheduled editorial beat as the absolute timing window.
[[nodiscard]] std::expected<HandwritingMotionProgram, diagnostics::Error>
compile_handwriting_for_beat(const ScheduledEditorialBeat &beat,
                             HandwritingPreset preset, MotionEase ease,
                             std::span<const HandwritingLineSpec> lines,
                             double pause_fraction = 0.08,
                             double dash_on_fraction = 0.55,
                             double scribble_overshoot = 0.15);

// Sample every stroke at an absolute shot-local frame.
[[nodiscard]] std::expected<std::vector<HandwritingStrokeSample>,
                            diagnostics::Error>
sample_handwriting_motion(const HandwritingMotionProgram &program,
                          std::uint64_t frame);

// Lower to DirectedTracks with property "ink_progress" for CDS publish.
[[nodiscard]] std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_handwriting_tracks(const HandwritingMotionProgram &program,
                         std::string_view shot_id);

} // namespace instantee::direction
