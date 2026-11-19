#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
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

// Overflow-safe estimate for one tightly packed RGBA8 frame. Used to preflight
// ResourceBudget::max_accounted_bytes before allocation.
[[nodiscard]] inline std::expected<std::uint64_t, diagnostics::Error>
estimate_rgba8_bytes(std::uint32_t width_px, std::uint32_t height_px) noexcept {
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::invalid_input,
        diagnostics::ErrorKind::invalid_input, "invalid_frame_extent",
        "resource_budget", "frame width and height must be > 0"});
  }
  const std::uint64_t width = width_px;
  const std::uint64_t height = height_px;
  if (width > (std::numeric_limits<std::uint64_t>::max() / height)) {
    return std::unexpected(
        diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                           diagnostics::ErrorKind::resource_budget_exceeded,
                           "resource_budget_exceeded", "resource_budget",
                           "frame pixel count overflows budget arithmetic"});
  }
  const std::uint64_t pixels = width * height;
  if (pixels > (std::numeric_limits<std::uint64_t>::max() / 4U)) {
    return std::unexpected(
        diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                           diagnostics::ErrorKind::resource_budget_exceeded,
                           "resource_budget_exceeded", "resource_budget",
                           "frame byte size overflows budget arithmetic"});
  }
  return pixels * 4U;
}

// Sole owner of a compiled scene for a render/evaluate job. Also tracks the
// production phase machine, cancel gate, first sticky error, and accounted
// resource budget. Workers and GPU resources stay outside this type.
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
  // Bytes remaining under max_accounted_bytes (UINT64_MAX when unset).
  [[nodiscard]] std::uint64_t remaining_accounted_bytes() const noexcept {
    if (budget_.max_accounted_bytes == 0U) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    if (accounted_bytes_ >= budget_.max_accounted_bytes) {
      return 0U;
    }
    return budget_.max_accounted_bytes - accounted_bytes_;
  }
  // True when acquiring `frame_bytes` would exceed the declared ceiling.
  [[nodiscard]] bool
  would_exceed_accounted(std::uint64_t frame_bytes) const noexcept {
    if (budget_.max_accounted_bytes == 0U) {
      return false;
    }
    const auto next = accounted_bytes_ + frame_bytes;
    return next < accounted_bytes_ || next > budget_.max_accounted_bytes;
  }
  [[nodiscard]] std::uint64_t in_flight_frames() const noexcept {
    return in_flight_frames_;
  }
  // First failure observed on this job. Later faults do not overwrite it.
  [[nodiscard]] const std::optional<diagnostics::Error> &
  first_error() const noexcept {
    return first_error_;
  }

  [[nodiscard]] std::unique_ptr<const CompiledScene> release_scene() noexcept {
    return std::move(scene_);
  }

  // Latches the first error without changing phase. Used by orchestrators that
  // observe a domain failure before calling fail()/note_publish_outcome().
  void note_error(diagnostics::Error error) noexcept;

  // Advances the phase machine. Rejects illegal transitions and observes stop
  // before entering Publishing (cancel wins). After Publishing begins, stop
  // cannot roll back publication.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  enter(RenderJobPhase next, std::stop_token stop = {});

  // Marks a terminal failure from the current non-terminal phase. Optional
  // cause is latched as first_error when none is stored yet. From `published`
  // this becomes published_report_failed (MP4 remains acknowledged).
  [[nodiscard]] std::expected<void, diagnostics::Error>
  fail(std::optional<diagnostics::Error> cause = std::nullopt) noexcept;

  // Cancel before Publishing. After the publish gate this returns an error and
  // leaves the published* phase unchanged (never claims the video is absent).
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

  // Map OutputTransaction durability into published* phases. Latches a sticky
  // first_error for unpublished failure, durability-unknown, or report-failed.
  [[nodiscard]] std::expected<void, diagnostics::Error> note_publish_outcome(
      bool published, bool durability_confirmed, bool report_failed,
      std::optional<diagnostics::Error> cause = std::nullopt) noexcept;

private:
  void latch_error(diagnostics::Error error) noexcept;

  std::unique_ptr<const CompiledScene> scene_;
  ResourceBudget budget_{};
  RenderJobPhase phase_ = RenderJobPhase::created;
  std::uint64_t frames_committed_ = 0;
  std::uint64_t accounted_bytes_ = 0;
  std::uint64_t in_flight_frames_ = 0;
  std::optional<diagnostics::Error> first_error_{};
};

} // namespace instantee::runtime
