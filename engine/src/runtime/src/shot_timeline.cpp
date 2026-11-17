#include <instantee/runtime/shot_timeline.hpp>

#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

} // namespace

std::expected<ShotTimeline, diagnostics::Error>
make_shot_timeline(std::span<const RuntimeShot> shots,
                   core::FrameIndex duration_frames) {
  if (shots.empty()) {
    return std::unexpected(
        err("empty_shots", "Shot timeline requires at least one shot"));
  }
  if (duration_frames.value() == 0U) {
    return std::unexpected(
        err("empty_duration", "Film duration_frames must be > 0"));
  }

  ShotTimeline timeline;
  timeline.duration_frames_ = duration_frames;
  timeline.intervals_.reserve(shots.size());

  std::uint64_t cursor = 0;
  for (std::uint32_t index = 0; index < shots.size(); ++index) {
    const RuntimeShot &shot = shots[index];
    if (shot.duration_frames == 0U) {
      return std::unexpected(
          err("empty_shot_duration", "Shot duration_frames must be > 0"));
    }
    if (shot.start_frame != cursor) {
      return std::unexpected(
          err("shot_timeline_gap",
              "Shots must form a contiguous half-open timeline from frame 0"));
    }
    auto end = core::FrameIndex::checked_add(core::FrameIndex{shot.start_frame},
                                             shot.duration_frames);
    if (!end) {
      return std::unexpected(
          err("frame_overflow", "Shot end frame overflows FrameIndex"));
    }
    ShotInterval interval;
    interval.id = shot.id;
    interval.shot_index = index;
    interval.start = core::FrameIndex{shot.start_frame};
    interval.end_exclusive = *end;
    timeline.intervals_.push_back(std::move(interval));
    cursor = end->value();
  }

  if (cursor != duration_frames.value()) {
    return std::unexpected(
        err("shot_duration_mismatch",
            "Shot durations do not cover duration_frames exactly"));
  }
  return timeline;
}

std::expected<ShotLocalFrame, diagnostics::Error>
ShotTimeline::resolve_global(core::FrameIndex global) const {
  if (global.value() >= duration_frames_.value()) {
    return std::unexpected(
        err("frame_out_of_range", "Global frame is outside the film duration"));
  }
  // Contiguous half-open intervals: binary search by end_exclusive.
  std::size_t lo = 0;
  std::size_t hi = intervals_.size();
  while (lo < hi) {
    const std::size_t mid = lo + (hi - lo) / 2;
    if (intervals_[mid].end_exclusive.value() <= global.value()) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo >= intervals_.size() ||
      global.value() < intervals_[lo].start.value()) {
    // Unreachable for a validated contiguous timeline covering [0, duration).
    return std::unexpected(
        err("shot_lookup_failed", "No shot owns the global frame"));
  }
  const ShotInterval &interval = intervals_[lo];
  return ShotLocalFrame{
      .shot_index = interval.shot_index,
      .local = core::FrameIndex{global.value() - interval.start.value()},
  };
}

std::expected<core::FrameIndex, diagnostics::Error>
ShotTimeline::to_global(std::uint32_t shot_index,
                        core::FrameIndex local) const {
  if (shot_index >= intervals_.size()) {
    return std::unexpected(
        err("shot_index_out_of_range", "Unknown shot_index"));
  }
  const ShotInterval &interval = intervals_[shot_index];
  const std::uint64_t duration =
      interval.end_exclusive.value() - interval.start.value();
  if (local.value() >= duration) {
    return std::unexpected(
        err("local_frame_out_of_range",
            "Shot-local frame is outside the shot half-open duration"));
  }
  return core::FrameIndex::checked_add(interval.start, local.value());
}

} // namespace instantee::runtime
