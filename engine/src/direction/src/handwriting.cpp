#include <instantee/direction/handwriting.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "handwriting", message};
}

[[nodiscard]] double clamp01(double v) noexcept {
  if (!(v > 0.0)) {
    return 0.0;
  }
  if (v > 1.0) {
    return 1.0;
  }
  return v;
}

[[nodiscard]] double apply_ease(MotionEase ease, double t) noexcept {
  const double u = clamp01(t);
  switch (ease) {
  case MotionEase::linear:
    return u;
  case MotionEase::ease_in:
    return u * u;
  case MotionEase::ease_out:
    return 1.0 - (1.0 - u) * (1.0 - u);
  case MotionEase::ease_in_out:
    return u * u * (3.0 - 2.0 * u);
  case MotionEase::spring: {
    const double decay = std::exp(-7.0 * u);
    const double wave = std::cos(10.0 * u);
    return clamp01(1.0 - decay * wave * (1.0 - u));
  }
  }
  return u;
}

[[nodiscard]] double scribble_progress(double eased,
                                       double overshoot) noexcept {
  // Rise to 1+overshoot by mid window, then settle back to 1.0.
  if (eased <= 0.5) {
    const double local = eased / 0.5;
    return local * (1.0 + overshoot);
  }
  const double local = (eased - 0.5) / 0.5;
  const double peak = 1.0 + overshoot;
  return peak + (1.0 - peak) * local;
}

[[nodiscard]] bool dash_on(double eased, double on_fraction) noexcept {
  // Two dash cycles across the line window; gaps are lifts.
  const double cycles = 2.0;
  const double phase = std::fmod(eased * cycles, 1.0);
  return phase <= on_fraction + 1e-12;
}

void push_key(DirectedTrack &track, std::uint64_t frame, double value) {
  if (!track.keyframes.empty() && track.keyframes.back().frame == frame) {
    track.keyframes.back().value = value;
    return;
  }
  track.keyframes.push_back(DirectedKeyframe{.frame = frame, .value = value});
}

} // namespace

std::expected<HandwritingMotionParams, diagnostics::Error>
validate_handwriting_params(HandwritingMotionParams params) {
  if (params.beat_duration_frames == 0) {
    return std::unexpected(make_invalid(
        "empty_beat", "handwriting beat_duration_frames must be > 0"));
  }
  if (!std::isfinite(params.pause_fraction) || params.pause_fraction < 0.0 ||
      params.pause_fraction > 0.5) {
    return std::unexpected(
        make_invalid("bad_pause", "pause_fraction must be finite in [0, 0.5]"));
  }
  if (!std::isfinite(params.dash_on_fraction) ||
      !(params.dash_on_fraction > 0.0) || params.dash_on_fraction > 1.0) {
    return std::unexpected(
        make_invalid("bad_dash", "dash_on_fraction must be finite in (0, 1]"));
  }
  if (!std::isfinite(params.scribble_overshoot) ||
      params.scribble_overshoot < 0.0 || params.scribble_overshoot > 0.5) {
    return std::unexpected(make_invalid(
        "bad_overshoot", "scribble_overshoot must be finite in [0, 0.5]"));
  }
  if (params.beat_start_frame >
      std::numeric_limits<std::uint64_t>::max() - params.beat_duration_frames) {
    return std::unexpected(
        make_invalid("beat_overflow", "beat window overflows uint64"));
  }
  return params;
}

std::expected<HandwritingMotionParams, diagnostics::Error>
make_handwriting_params(std::string_view preset_id, const MotionToken &token,
                        std::uint64_t beat_start_frame,
                        std::uint64_t beat_duration_frames) {
  HandwritingMotionParams params;
  params.beat_start_frame = beat_start_frame;
  params.beat_duration_frames = beat_duration_frames;
  params.ease = token.ease;
  if (preset_id == "handwriting_line") {
    params.preset = HandwritingPreset::line_by_line;
  } else if (preset_id == "handwriting_dash") {
    params.preset = HandwritingPreset::dashed_sketch;
  } else if (preset_id == "handwriting_scribble") {
    params.preset = HandwritingPreset::correction_scribble;
  } else {
    return std::unexpected(
        make_invalid("unknown_handwriting_preset",
                     "Expected handwriting_line, handwriting_dash, or "
                     "handwriting_scribble"));
  }
  return validate_handwriting_params(std::move(params));
}

std::expected<HandwritingMotionProgram, diagnostics::Error>
compile_handwriting_motion(const HandwritingMotionParams &params,
                           std::span<const HandwritingLineSpec> lines) {
  auto valid = validate_handwriting_params(params);
  if (!valid) {
    return std::unexpected(valid.error());
  }
  if (lines.empty()) {
    return std::unexpected(make_invalid(
        "empty_lines", "handwriting requires at least one stroke line"));
  }

  double weight_sum = 0.0;
  for (const auto &line : lines) {
    if (line.stroke_id.empty()) {
      return std::unexpected(make_invalid(
          "empty_stroke_id", "each handwriting line needs a stroke_id"));
    }
    if (!std::isfinite(line.weight) || !(line.weight > 0.0)) {
      return std::unexpected(make_invalid(
          "bad_weight", "handwriting line weight must be finite and > 0"));
    }
    weight_sum += line.weight;
  }

  const std::size_t n = lines.size();
  const std::uint64_t beat = valid->beat_duration_frames;
  // Pause frames between lines (n-1 gaps). Keep at least 1 frame per line.
  std::uint64_t pause_frames = 0;
  if (n > 1 && valid->pause_fraction > 0.0) {
    const double raw = static_cast<double>(beat) * valid->pause_fraction;
    pause_frames = static_cast<std::uint64_t>(std::llround(raw));
    const std::uint64_t max_pause_budget = beat > n ? beat - n : 0;
    const std::uint64_t max_total_pause =
        max_pause_budget / static_cast<std::uint64_t>(n - 1);
    pause_frames = std::min(pause_frames, max_total_pause);
  }
  const std::uint64_t total_pause =
      pause_frames * static_cast<std::uint64_t>(n > 1 ? n - 1 : 0);
  if (total_pause >= beat) {
    return std::unexpected(make_invalid(
        "pause_too_large", "pauses leave no frames for handwriting strokes"));
  }
  const std::uint64_t draw_budget = beat - total_pause;

  HandwritingMotionProgram program;
  program.params = *valid;
  program.beat_end_frame = valid->beat_start_frame + beat;
  program.lines.reserve(n);

  std::uint64_t cursor = valid->beat_start_frame;
  std::uint64_t assigned = 0;
  for (std::size_t i = 0; i < n; ++i) {
    HandwritingLineWindow win;
    win.stroke_id = lines[i].stroke_id;
    win.start_frame = cursor;
    if (i + 1 == n) {
      win.duration_frames = draw_budget - assigned;
    } else {
      const double share = lines[i].weight / weight_sum;
      auto dur = static_cast<std::uint64_t>(
          std::llround(static_cast<double>(draw_budget) * share));
      if (dur == 0) {
        dur = 1;
      }
      // Leave at least one frame for each remaining line.
      const std::uint64_t remain_lines = static_cast<std::uint64_t>(n - i - 1);
      const std::uint64_t max_dur = draw_budget - assigned - remain_lines;
      win.duration_frames = std::min(dur, max_dur);
      assigned += win.duration_frames;
    }
    if (win.duration_frames == 0) {
      return std::unexpected(make_invalid(
          "zero_line_duration", "handwriting line received zero frames"));
    }
    cursor += win.duration_frames;
    if (i + 1 < n) {
      cursor += pause_frames;
    }
    program.lines.push_back(std::move(win));
  }

  if (cursor != program.beat_end_frame) {
    // Absorb rounding into the last line so the pack still fills the beat.
    auto &last = program.lines.back();
    if (cursor < program.beat_end_frame) {
      last.duration_frames += program.beat_end_frame - cursor;
    }
  }
  return program;
}

std::expected<HandwritingMotionProgram, diagnostics::Error>
compile_handwriting_for_beat(const ScheduledEditorialBeat &beat,
                             HandwritingPreset preset, MotionEase ease,
                             std::span<const HandwritingLineSpec> lines,
                             double pause_fraction, double dash_on_fraction,
                             double scribble_overshoot) {
  HandwritingMotionParams params;
  params.preset = preset;
  params.ease = ease;
  params.beat_start_frame = beat.start_frame;
  params.beat_duration_frames = beat.duration_frames;
  params.pause_fraction = pause_fraction;
  params.dash_on_fraction = dash_on_fraction;
  params.scribble_overshoot = scribble_overshoot;
  return compile_handwriting_motion(params, lines);
}

std::expected<std::vector<HandwritingStrokeSample>, diagnostics::Error>
sample_handwriting_motion(const HandwritingMotionProgram &program,
                          std::uint64_t frame) {
  if (program.lines.empty()) {
    return std::unexpected(
        make_invalid("invalid_program", "handwriting program has no lines"));
  }

  std::vector<HandwritingStrokeSample> out;
  out.reserve(program.lines.size());
  for (const HandwritingLineWindow &win : program.lines) {
    HandwritingStrokeSample sample;
    sample.stroke_id = win.stroke_id;
    if (win.duration_frames == 0) {
      return std::unexpected(
          make_invalid("invalid_program", "line duration_frames must be > 0"));
    }

    double linear = 0.0;
    if (frame <= win.start_frame) {
      linear = 0.0;
    } else if (frame >= win.start_frame + win.duration_frames) {
      linear = 1.0;
    } else {
      linear = static_cast<double>(frame - win.start_frame) /
               static_cast<double>(win.duration_frames);
    }
    const double eased = apply_ease(program.params.ease, linear);

    switch (program.params.preset) {
    case HandwritingPreset::line_by_line:
      sample.progress = eased;
      sample.dash_visible = true;
      break;
    case HandwritingPreset::dashed_sketch:
      sample.progress = eased;
      sample.dash_visible =
          eased <= 0.0 ? false
                       : dash_on(eased, program.params.dash_on_fraction);
      break;
    case HandwritingPreset::correction_scribble:
      sample.progress =
          scribble_progress(eased, program.params.scribble_overshoot);
      sample.dash_visible = true;
      break;
    }
    out.push_back(std::move(sample));
  }
  return out;
}

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_handwriting_tracks(const HandwritingMotionProgram &program,
                         std::string_view shot_id) {
  if (shot_id.empty()) {
    return std::unexpected(
        make_invalid("empty_shot_id", "handwriting lowering needs a shot_id"));
  }
  if (program.lines.empty()) {
    return std::unexpected(
        make_invalid("invalid_program", "handwriting program has no lines"));
  }

  std::vector<DirectedTrack> tracks;
  tracks.reserve(program.lines.size());
  for (std::size_t i = 0; i < program.lines.size(); ++i) {
    const auto &win = program.lines[i];
    DirectedTrack track;
    track.id = std::string{shot_id} + "/handwriting/" + win.stroke_id;
    track.target_id = win.stroke_id;
    track.property = "ink_progress";

    // Hold 0 before the line window (and before the beat).
    if (win.start_frame > 0) {
      push_key(track, win.start_frame - 1, 0.0);
    }
    push_key(track, win.start_frame, 0.0);

    const std::uint64_t mid = win.start_frame + win.duration_frames / 2;
    const std::uint64_t end = win.start_frame + win.duration_frames;

    switch (program.params.preset) {
    case HandwritingPreset::line_by_line:
    case HandwritingPreset::dashed_sketch:
      push_key(track, end > 0 ? end - 1 : 0, 1.0);
      if (end > win.start_frame) {
        push_key(track, end, 1.0);
      }
      break;
    case HandwritingPreset::correction_scribble: {
      const double peak = 1.0 + program.params.scribble_overshoot;
      push_key(track, mid, peak);
      push_key(track, end > 0 ? end - 1 : 0, 1.0);
      if (end > win.start_frame) {
        push_key(track, end, 1.0);
      }
      break;
    }
    }

    // After the beat, keep settled value so later beats do not re-hide ink.
    if (program.beat_end_frame > end) {
      push_key(track, program.beat_end_frame, 1.0);
    }
    tracks.push_back(std::move(track));
    (void)i;
  }
  return tracks;
}

} // namespace instantee::direction
