#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::runtime {

// Half-open film range owned by one shot: [start, end_exclusive).
struct ShotInterval {
  std::string id;
  std::uint32_t shot_index = 0;
  core::FrameIndex start{};
  core::FrameIndex end_exclusive{};
};

struct ShotLocalFrame {
  std::uint32_t shot_index = 0;
  core::FrameIndex local{}; // offset from shot.start; in [0, duration)
};

// Contiguous sequential shots covering [0, duration_frames). Built once from
// RuntimeShot rows; lookups are pure functions of the validated intervals.
class ShotTimeline {
public:
  [[nodiscard]] core::FrameIndex duration_frames() const noexcept {
    return duration_frames_;
  }

  [[nodiscard]] std::span<const ShotInterval> intervals() const noexcept {
    return intervals_;
  }

  // Owns global film frame → shot + local offset. Rejects frame >= duration.
  [[nodiscard]] std::expected<ShotLocalFrame, diagnostics::Error>
  resolve_global(core::FrameIndex global) const;

  // Shot-local offset → absolute film frame. Rejects local outside the shot.
  [[nodiscard]] std::expected<core::FrameIndex, diagnostics::Error>
  to_global(std::uint32_t shot_index, core::FrameIndex local) const;

private:
  friend std::expected<ShotTimeline, diagnostics::Error>
  make_shot_timeline(std::span<const RuntimeShot> shots,
                     core::FrameIndex duration_frames);

  core::FrameIndex duration_frames_{};
  std::vector<ShotInterval> intervals_;
};

// Requires: non-empty shots, each duration > 0, starts form a contiguous
// sequence from 0, and sum(durations) == duration_frames (no gaps/overlaps).
[[nodiscard]] std::expected<ShotTimeline, diagnostics::Error>
make_shot_timeline(std::span<const RuntimeShot> shots,
                   core::FrameIndex duration_frames);

[[nodiscard]] inline std::expected<ShotTimeline, diagnostics::Error>
make_shot_timeline(const CompiledScene &scene) {
  return make_shot_timeline(scene.shots(), scene.duration_frames());
}

} // namespace instantee::runtime
