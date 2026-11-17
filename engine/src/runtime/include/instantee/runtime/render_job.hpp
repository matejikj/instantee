#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <string_view>
#include <utility>

namespace instantee::runtime {

// Production render-job phases (CPP_RUNTIME_CONTRACTS §4). Cancel may win only
// before Publishing; after the publish gate the outcome stays published*.
enum class RenderJobPhase : std::uint8_t {
  created = 1,
  preparing,
  rendering,
  finishing,
  publishing,
  published,
  published_durability_unknown,
  published_report_failed,
  completed,
  failed,
  cancelled,
};

[[nodiscard]] constexpr std::string_view
render_job_phase_id(RenderJobPhase phase) noexcept {
  switch (phase) {
  case RenderJobPhase::created:
    return "created";
  case RenderJobPhase::preparing:
    return "preparing";
  case RenderJobPhase::rendering:
    return "rendering";
  case RenderJobPhase::finishing:
    return "finishing";
  case RenderJobPhase::publishing:
    return "publishing";
  case RenderJobPhase::published:
    return "published";
  case RenderJobPhase::published_durability_unknown:
    return "published_durability_unknown";
  case RenderJobPhase::published_report_failed:
    return "published_report_failed";
  case RenderJobPhase::completed:
    return "completed";
  case RenderJobPhase::failed:
    return "failed";
  case RenderJobPhase::cancelled:
    return "cancelled";
  }
  return "unknown";
}

[[nodiscard]] constexpr bool is_terminal_phase(RenderJobPhase phase) noexcept {
  switch (phase) {
  case RenderJobPhase::completed:
  case RenderJobPhase::failed:
  case RenderJobPhase::cancelled:
  case RenderJobPhase::published_durability_unknown:
  case RenderJobPhase::published_report_failed:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] constexpr bool is_published_phase(RenderJobPhase phase) noexcept {
  switch (phase) {
  case RenderJobPhase::published:
  case RenderJobPhase::published_durability_unknown:
  case RenderJobPhase::published_report_failed:
  case RenderJobPhase::completed:
    return true;
  default:
    return false;
  }
}

// Declared memory/resource ceilings for one job. Zero means "unset" except
// max_in_flight_frames which defaults to 2 when constructed empty.
struct ResourceBudget {
  // Hard cap on frames this job may render (0 = unlimited / scene duration).
  std::uint64_t max_output_frames = 0;
  // Accounted CPU RGBA working set ceiling (bytes). Exceed → budget error.
  std::uint64_t max_accounted_bytes = 0;
  // Frames simultaneously owned between evaluate and sink-accept.
  std::uint64_t max_in_flight_frames = 2;
};

// Sole owner of a compiled scene for a render/evaluate job. Also tracks the
// production phase machine, cancel gate, and accounted resource budget.
// Workers and GPU resources stay outside this type.
class RenderJob {
public:
  explicit RenderJob(std::unique_ptr<const CompiledScene> scene,
                     ResourceBudget budget = {}) noexcept;

  RenderJob(const RenderJob &) = delete;
  RenderJob &operator=(const RenderJob &) = delete;
  RenderJob(RenderJob &&) noexcept = default;
  RenderJob &operator=(RenderJob &&) noexcept = default;
  ~RenderJob() = default;

  [[nodiscard]] bool has_scene() const noexcept { return scene_ != nullptr; }
  [[nodiscard]] const CompiledScene &scene() const noexcept { return *scene_; }
  [[nodiscard]] RenderJobPhase phase() const noexcept { return phase_; }
  [[nodiscard]] const ResourceBudget &budget() const noexcept {
    return budget_;
  }
  [[nodiscard]] std::uint64_t frames_committed() const noexcept {
    return frames_committed_;
  }
  [[nodiscard]] std::uint64_t accounted_bytes() const noexcept {
    return accounted_bytes_;
  }
  [[nodiscard]] std::uint64_t in_flight_frames() const noexcept {
    return in_flight_frames_;
  }

  [[nodiscard]] std::unique_ptr<const CompiledScene> release_scene() noexcept {
    return std::move(scene_);
  }

  // Advances the phase machine. Rejects illegal transitions and observes stop
  // before entering Publishing (cancel wins). After Publishing begins, stop
  // cannot roll back publication.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  enter(RenderJobPhase next, std::stop_token stop = {});

  // Marks a terminal failure from the current non-terminal phase.
  [[nodiscard]] std::expected<void, diagnostics::Error> fail() noexcept;

  // Cancel before Publishing. After the publish gate this returns an error and
  // leaves the published* phase unchanged.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  cancel(std::stop_token stop = {}) noexcept;

  // Record that one frame is in-flight (evaluate→sink). Enforces in-flight and
  // accounted-byte budgets.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  acquire_frame_slot(std::uint64_t accounted_bytes);

  // Release one in-flight slot after sink acceptance or abandon. Pass the same
  // byte count used in acquire_frame_slot so accounted usage tracks
  // concurrency.
  void release_frame_slot(std::uint64_t frame_bytes) noexcept;

  // Count a successfully appended output frame against max_output_frames.
  [[nodiscard]] std::expected<void, diagnostics::Error> note_frame_appended();

  // Map OutputTransaction durability into published* phases.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  note_publish_outcome(bool published, bool durability_confirmed,
                       bool report_failed) noexcept;

private:
  std::unique_ptr<const CompiledScene> scene_;
  ResourceBudget budget_{};
  RenderJobPhase phase_ = RenderJobPhase::created;
  std::uint64_t frames_committed_ = 0;
  std::uint64_t accounted_bytes_ = 0;
  std::uint64_t in_flight_frames_ = 0;
};

} // namespace instantee::runtime
