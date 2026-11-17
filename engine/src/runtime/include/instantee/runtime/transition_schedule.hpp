#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/shot_timeline.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::runtime {

enum class TransitionBlend : std::uint8_t { cut = 0, dissolve, wipe };

// Authoring/runtime description of one explicit transition. Absolute film
// coordinates; does not extend output length (shots already pack the film).
struct TransitionSpanDesc {
  std::string id;
  std::uint32_t from_shot_index = 0;
  std::uint32_t to_shot_index = 0;
  core::FrameIndex start{};
  std::uint64_t duration_frames = 0; // 0 ⇒ instantaneous cut at `start`
  TransitionBlend blend = TransitionBlend::cut;
};

struct TransitionSpan {
  std::string id;
  std::uint32_t from_shot_index = 0;
  std::uint32_t to_shot_index = 0;
  core::FrameIndex start{};
  core::FrameIndex end_exclusive{};
  TransitionBlend blend = TransitionBlend::cut;
};

// Dual contribution during an overlap/dissolve/wipe. mix_t ∈ [0, 1] is the
// unit progress across the span (0 = fully from_shot, 1 = fully to_shot).
struct TransitionMix {
  std::uint32_t from_shot_index = 0;
  std::uint32_t to_shot_index = 0;
  core::FrameIndex from_local{};
  core::FrameIndex to_local{};
  double mix_t = 0.0;
  TransitionBlend blend = TransitionBlend::dissolve;
};

struct OutputFrameSchedule {
  core::FrameIndex output_frame{};
  core::RationalTime pts{};
  std::uint32_t primary_shot_index = 0;
  core::FrameIndex primary_local{};
  std::optional<TransitionMix> mix; // set only inside a non-cut span
};

// Validated transition list over a packed ShotTimeline. Output duration equals
// the shot timeline duration — transitions never append or duplicate frames.
class TransitionSchedule {
public:
  [[nodiscard]] core::FrameIndex output_duration() const noexcept {
    return timeline_.duration_frames();
  }

  [[nodiscard]] const ShotTimeline &timeline() const noexcept {
    return timeline_;
  }

  [[nodiscard]] std::span<const TransitionSpan> spans() const noexcept {
    return spans_;
  }

  [[nodiscard]] std::expected<OutputFrameSchedule, diagnostics::Error>
  schedule_frame(core::FrameIndex output_frame, core::FrameRate rate) const;

private:
  friend std::expected<TransitionSchedule, diagnostics::Error>
  make_transition_schedule(ShotTimeline timeline,
                           std::span<const TransitionSpanDesc> spans);

  ShotTimeline timeline_{};
  std::vector<TransitionSpan> spans_;
};

[[nodiscard]] std::expected<TransitionSchedule, diagnostics::Error>
make_transition_schedule(ShotTimeline timeline,
                         std::span<const TransitionSpanDesc> spans);

} // namespace instantee::runtime
