#include <instantee/direction/headline_reveal.hpp>

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
                            "headline_reveal", message};
}

[[nodiscard]] bool finite_unit(double v) noexcept { return std::isfinite(v); }

[[nodiscard]] double apply_ease(MotionEase ease, double t) noexcept {
  t = std::clamp(t, 0.0, 1.0);
  switch (ease) {
  case MotionEase::linear:
    return t;
  case MotionEase::ease_in:
    return t * t;
  case MotionEase::ease_out: {
    const double u = 1.0 - t;
    return 1.0 - u * u;
  }
  case MotionEase::ease_in_out:
    if (t < 0.5) {
      return 2.0 * t * t;
    }
    {
      const double u = -2.0 * t + 2.0;
      return 1.0 - (u * u) / 2.0;
    }
  case MotionEase::spring: {
    // Bounded settle curve for compile-time presets (not a runtime spring
    // solver). Stays in [0, 1] and reaches 1 at t=1.
    const double decay = std::exp(-7.0 * t);
    const double wave = std::cos(10.0 * t);
    return std::clamp(1.0 - decay * wave * (1.0 - t), 0.0, 1.0);
  }
  }
  return t;
}

[[nodiscard]] std::expected<HeadlineRevealParams, diagnostics::Error>
validate_params(HeadlineRevealParams params) {
  if (params.duration_frames == 0) {
    return std::unexpected(make_invalid(
        "invalid_duration", "headline reveal duration_frames must be > 0"));
  }
  if (!finite_unit(params.intensity) || !(params.intensity > 0.0) ||
      params.intensity > 1.0) {
    return std::unexpected(make_invalid(
        "invalid_intensity", "headline reveal intensity must be in (0, 1]"));
  }
  return params;
}

[[nodiscard]] std::uint32_t
unit_local_duration(const HeadlineRevealParams &params,
                    std::uint32_t unit_count) noexcept {
  if (params.mode == HeadlineRevealMode::slam || unit_count <= 1 ||
      params.stagger_frames == 0) {
    return params.duration_frames;
  }
  const std::uint64_t last_start =
      static_cast<std::uint64_t>(unit_count - 1) * params.stagger_frames;
  if (last_start >= params.duration_frames) {
    return 1;
  }
  const std::uint32_t remaining =
      params.duration_frames - static_cast<std::uint32_t>(last_start);
  return std::max<std::uint32_t>(remaining, 1);
}

void push_key(DirectedTrack &track, std::uint64_t frame, double value) {
  if (!track.keyframes.empty() && track.keyframes.back().frame == frame) {
    track.keyframes.back().value = value;
    return;
  }
  track.keyframes.push_back(DirectedKeyframe{.frame = frame, .value = value});
}

} // namespace

std::expected<HeadlineRevealParams, diagnostics::Error>
make_headline_reveal_params(std::string_view preset_id,
                            const MotionToken &token) {
  HeadlineRevealParams params;
  params.ease = token.ease;
  params.duration_frames = token.duration_frames;
  params.intensity = token.intensity;
  params.stagger_frames = token.stagger_frames;

  if (preset_id == "headline_reveal") {
    params.mode = HeadlineRevealMode::word_stagger;
  } else if (preset_id == "mask_reveal") {
    params.mode = HeadlineRevealMode::mask_reveal;
  } else if (preset_id == "accent_slam") {
    params.mode = HeadlineRevealMode::slam;
    params.stagger_frames = 0;
  } else {
    return std::unexpected(
        make_invalid("unknown_headline_preset",
                     "Expected headline_reveal, mask_reveal, or accent_slam"));
  }
  return validate_params(std::move(params));
}

std::expected<HeadlineRevealParams, diagnostics::Error>
scale_headline_reveal_params(HeadlineRevealParams params, double intensity,
                             std::uint32_t duration_frames) {
  params.intensity = intensity;
  params.duration_frames = duration_frames;
  return validate_params(std::move(params));
}

std::expected<HeadlineRevealProgram, diagnostics::Error>
compile_headline_reveal(const HeadlineRevealParams &params,
                        std::uint32_t unit_count) {
  auto valid = validate_params(params);
  if (!valid) {
    return std::unexpected(valid.error());
  }
  if (unit_count == 0) {
    return std::unexpected(make_invalid(
        "empty_units", "headline reveal requires at least one kinetic unit"));
  }
  if (params.mode == HeadlineRevealMode::slam && unit_count != 1) {
    return std::unexpected(make_invalid(
        "slam_unit_count", "slam typography expects exactly one unit"));
  }

  const std::uint32_t local_dur = unit_local_duration(params, unit_count);
  HeadlineRevealProgram program;
  program.params = *valid;
  program.unit_count = unit_count;
  program.unit_start_frame.resize(unit_count);
  program.unit_duration_frames.resize(unit_count);

  std::uint32_t max_end = 0;
  for (std::uint32_t i = 0; i < unit_count; ++i) {
    std::uint32_t start = 0;
    if (params.mode != HeadlineRevealMode::slam && params.stagger_frames > 0) {
      const std::uint64_t raw =
          static_cast<std::uint64_t>(i) * params.stagger_frames;
      start = static_cast<std::uint32_t>(
          std::min<std::uint64_t>(raw, params.duration_frames - 1));
    }
    program.unit_start_frame[i] = start;
    program.unit_duration_frames[i] = local_dur;
    const std::uint64_t end = static_cast<std::uint64_t>(start) + local_dur;
    if (end > max_end) {
      max_end = end > std::numeric_limits<std::uint32_t>::max()
                    ? std::numeric_limits<std::uint32_t>::max()
                    : static_cast<std::uint32_t>(end);
    }
  }
  program.total_frames = std::max(params.duration_frames, max_end);
  return program;
}

std::expected<std::vector<HeadlineUnitFrame>, diagnostics::Error>
sample_headline_reveal(const HeadlineRevealProgram &program,
                       std::uint32_t frame) {
  if (program.unit_count == 0 ||
      program.unit_start_frame.size() != program.unit_count ||
      program.unit_duration_frames.size() != program.unit_count) {
    return std::unexpected(
        make_invalid("invalid_program", "headline reveal program is empty"));
  }

  const auto &params = program.params;
  const double peak = params.intensity;
  // Entrance slide: up to 24px at intensity 1 (26.6 fixed).
  const std::int32_t slide_26_6 =
      static_cast<std::int32_t>(std::lround(24.0 * 64.0 * peak));

  std::vector<HeadlineUnitFrame> out;
  out.resize(program.unit_count);
  for (std::uint32_t i = 0; i < program.unit_count; ++i) {
    HeadlineUnitFrame sample;
    sample.unit_index = i;
    const std::uint32_t start = program.unit_start_frame[i];
    const std::uint32_t dur = program.unit_duration_frames[i];
    if (dur == 0) {
      return std::unexpected(
          make_invalid("invalid_program", "unit duration_frames must be > 0"));
    }

    double linear = 0.0;
    if (frame <= start) {
      linear = 0.0;
    } else if (frame >= start + dur) {
      linear = 1.0;
    } else {
      linear = static_cast<double>(frame - start) / static_cast<double>(dur);
    }
    const double progress = apply_ease(params.ease, linear);
    sample.progress = progress;

    switch (params.mode) {
    case HeadlineRevealMode::mask_reveal:
      sample.mask = progress;
      sample.opacity = progress * peak;
      sample.scale = 1.0;
      sample.offset_y_26_6 = 0;
      break;
    case HeadlineRevealMode::word_stagger:
      sample.mask = progress;
      sample.opacity = progress * peak;
      sample.scale = 1.0;
      sample.offset_y_26_6 =
          static_cast<std::int32_t>(std::lround((1.0 - progress) * slide_26_6));
      break;
    case HeadlineRevealMode::slam: {
      // Overshoot then settle to 1.0; opacity follows intensity peak.
      const double overshoot = 1.0 + peak * 0.35;
      sample.scale = overshoot + (1.0 - overshoot) * progress;
      sample.opacity = progress * peak;
      sample.mask = progress;
      sample.offset_y_26_6 = 0;
      break;
    }
    }
    out[i] = sample;
  }
  return out;
}

std::expected<std::vector<DirectedTrack>, diagnostics::Error>
lower_headline_reveal_tracks(const HeadlineRevealProgram &program,
                             std::string_view shot_id,
                             std::span<const std::string> unit_target_ids) {
  if (unit_target_ids.size() != program.unit_count) {
    return std::unexpected(
        make_invalid("target_mismatch",
                     "unit_target_ids size must match compiled unit_count"));
  }
  if (shot_id.empty()) {
    return std::unexpected(
        make_invalid("invalid_shot_id", "shot_id must be non-empty"));
  }

  std::vector<DirectedTrack> tracks;
  tracks.reserve(program.unit_count * 3);

  for (std::uint32_t i = 0; i < program.unit_count; ++i) {
    const std::uint32_t start = program.unit_start_frame[i];
    const std::uint32_t dur = program.unit_duration_frames[i];
    const std::uint32_t end = start + dur;
    const auto at_start = sample_headline_reveal(program, start);
    const auto at_end = sample_headline_reveal(program, end);
    if (!at_start || !at_end) {
      return std::unexpected(at_start ? at_end.error() : at_start.error());
    }
    const HeadlineUnitFrame &a = (*at_start)[i];
    const HeadlineUnitFrame &b = (*at_end)[i];
    const std::string &target = unit_target_ids[i];
    const std::string prefix =
        std::string(shot_id) + "_hl_" + target + "_" + std::to_string(i);

    DirectedTrack opacity;
    opacity.id = prefix + "_opacity";
    opacity.target_id = target;
    opacity.property = "opacity";
    push_key(opacity, start, a.opacity);
    push_key(opacity, end, b.opacity);
    tracks.push_back(std::move(opacity));

    DirectedTrack mask;
    mask.id = prefix + "_mask";
    mask.target_id = target;
    mask.property = "mask";
    push_key(mask, start, a.mask);
    push_key(mask, end, b.mask);
    tracks.push_back(std::move(mask));

    if (program.params.mode == HeadlineRevealMode::slam) {
      DirectedTrack scale;
      scale.id = prefix + "_scale";
      scale.target_id = target;
      scale.property = "scale";
      push_key(scale, start, a.scale);
      push_key(scale, end, b.scale);
      tracks.push_back(std::move(scale));
    }
  }
  return tracks;
}

} // namespace instantee::direction
