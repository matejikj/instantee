#include <instantee/runtime/transition_schedule.hpp>

#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

bool adjacent_shots(std::uint32_t from_index, std::uint32_t to_index) noexcept {
  return to_index == from_index + 1U;
}

} // namespace

std::expected<TransitionSchedule, diagnostics::Error>
make_transition_schedule(ShotTimeline timeline,
                         std::span<const TransitionSpanDesc> spans) {
  const auto intervals = timeline.intervals();
  if (intervals.empty()) {
    return std::unexpected(
        err("empty_shots", "Transition schedule requires a shot timeline"));
  }

  TransitionSchedule schedule;
  schedule.timeline_ = std::move(timeline);
  schedule.spans_.reserve(spans.size());

  for (const TransitionSpanDesc &desc : spans) {
    if (desc.id.empty()) {
      return std::unexpected(
          err("empty_id", "Transition id must be non-empty"));
    }
    if (desc.from_shot_index >= intervals.size() ||
        desc.to_shot_index >= intervals.size()) {
      return std::unexpected(
          err("shot_index_out_of_range", "Transition references unknown shot"));
    }
    if (!adjacent_shots(desc.from_shot_index, desc.to_shot_index)) {
      return std::unexpected(err(
          "non_adjacent_transition",
          "Transitions must connect consecutive shots on the packed timeline"));
    }

    const ShotInterval &from = intervals[desc.from_shot_index];
    const ShotInterval &to = intervals[desc.to_shot_index];
    const core::FrameIndex boundary = to.start; // == from.end_exclusive

    if (desc.blend == TransitionBlend::cut) {
      if (desc.duration_frames != 0U) {
        return std::unexpected(
            err("cut_requires_zero_duration",
                "Cut transitions must have duration_frames == 0"));
      }
      if (desc.start != boundary) {
        return std::unexpected(
            err("cut_not_at_boundary",
                "Cut transitions must sit exactly at the shot boundary"));
      }
    } else {
      if (desc.duration_frames == 0U) {
        return std::unexpected(
            err("overlap_requires_positive_duration",
                "Overlap transitions must have duration_frames > 0"));
      }
    }

    auto end = core::FrameIndex::checked_add(desc.start, desc.duration_frames);
    if (!end) {
      return std::unexpected(
          err("frame_overflow", "Transition end frame overflows FrameIndex"));
    }
    if (end->value() > schedule.timeline_.duration_frames().value()) {
      return std::unexpected(
          err("transition_extends_duration",
              "Transition must not extend the packed film duration"));
    }
    if (desc.duration_frames > 0U) {
      // Overlap must straddle the cut without inventing frames outside either
      // shot: [start, end) intersects both half-open shot ranges.
      if (!(desc.start.value() < boundary.value() &&
            end->value() > boundary.value())) {
        return std::unexpected(
            err("transition_misses_boundary",
                "Overlap transition must straddle the shot boundary"));
      }
      if (desc.start.value() < from.start.value() ||
          end->value() > to.end_exclusive.value()) {
        return std::unexpected(
            err("transition_outside_shots",
                "Transition span must stay inside the from/to shot ranges"));
      }
    }

    TransitionSpan span;
    span.id = desc.id;
    span.from_shot_index = desc.from_shot_index;
    span.to_shot_index = desc.to_shot_index;
    span.start = desc.start;
    span.end_exclusive = *end;
    span.blend = desc.blend;
    schedule.spans_.push_back(std::move(span));
  }

  return schedule;
}

std::expected<OutputFrameSchedule, diagnostics::Error>
TransitionSchedule::schedule_frame(core::FrameIndex output_frame,
                                   core::FrameRate rate) const {
  auto local = timeline_.resolve_global(output_frame);
  if (!local) {
    return std::unexpected(local.error());
  }

  auto clock = core::FrameClock::create(rate);
  if (!clock) {
    return std::unexpected(clock.error());
  }
  auto pts = clock->pts(output_frame);
  if (!pts) {
    return std::unexpected(pts.error());
  }

  OutputFrameSchedule out;
  out.output_frame = output_frame;
  out.pts = *pts;
  out.primary_shot_index = local->shot_index;
  out.primary_local = local->local;

  for (const TransitionSpan &span : spans_) {
    if (span.blend == TransitionBlend::cut) {
      continue;
    }
    if (output_frame.value() < span.start.value() ||
        output_frame.value() >= span.end_exclusive.value()) {
      continue;
    }
    const ShotInterval &from = timeline_.intervals()[span.from_shot_index];
    const ShotInterval &to = timeline_.intervals()[span.to_shot_index];
    const core::FrameIndex boundary = to.start;
    const std::uint64_t span_len =
        span.end_exclusive.value() - span.start.value();
    const double mix_t =
        static_cast<double>(output_frame.value() - span.start.value()) /
        static_cast<double>(span_len);

    // Before the cut, sample the start of `to` by dissolve progress. After the
    // cut, hold the last frame of `from` while `to` advances normally.
    const std::uint64_t from_last = boundary.value() - from.start.value() - 1U;
    const std::uint64_t from_local_value =
        output_frame.value() < boundary.value()
            ? (output_frame.value() - from.start.value())
            : from_last;
    const std::uint64_t to_local_value =
        output_frame.value() < boundary.value()
            ? (output_frame.value() - span.start.value())
            : (output_frame.value() - to.start.value());

    TransitionMix mix;
    mix.from_shot_index = span.from_shot_index;
    mix.to_shot_index = span.to_shot_index;
    mix.from_local = core::FrameIndex{from_local_value};
    mix.to_local = core::FrameIndex{to_local_value};
    mix.mix_t = mix_t;
    mix.blend = span.blend;
    out.mix = mix;
    break;
  }
  return out;
}

} // namespace instantee::runtime
