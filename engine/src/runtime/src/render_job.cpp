#include <instantee/runtime/render_job.hpp>

namespace instantee::runtime {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;

[[nodiscard]] bool is_legal_transition(RenderJobPhase from,
                                       RenderJobPhase to) noexcept {
  if (from == to) {
    return true;
  }
  switch (from) {
  case RenderJobPhase::created:
    return to == RenderJobPhase::preparing || to == RenderJobPhase::cancelled ||
           to == RenderJobPhase::failed;
  case RenderJobPhase::preparing:
    return to == RenderJobPhase::rendering || to == RenderJobPhase::cancelled ||
           to == RenderJobPhase::failed;
  case RenderJobPhase::rendering:
    return to == RenderJobPhase::finishing || to == RenderJobPhase::cancelled ||
           to == RenderJobPhase::failed;
  case RenderJobPhase::finishing:
    return to == RenderJobPhase::publishing ||
           to == RenderJobPhase::cancelled || to == RenderJobPhase::failed;
  case RenderJobPhase::publishing:
    return to == RenderJobPhase::published ||
           to == RenderJobPhase::published_durability_unknown ||
           to == RenderJobPhase::published_report_failed ||
           to == RenderJobPhase::failed;
  case RenderJobPhase::published:
    return to == RenderJobPhase::completed ||
           to == RenderJobPhase::published_report_failed;
  case RenderJobPhase::completed:
  case RenderJobPhase::failed:
  case RenderJobPhase::cancelled:
  case RenderJobPhase::published_durability_unknown:
  case RenderJobPhase::published_report_failed:
    return false;
  }
  return false;
}

[[nodiscard]] Error phase_error(std::string_view message) {
  return Error{ExitCode::invalid_input, ErrorKind::invalid_input,
               "render_job.illegal_phase", "render_job", message};
}

[[nodiscard]] Error budget_error(std::string_view code,
                                 std::string_view message) {
  return Error{ExitCode::resource_budget_exceeded,
               ErrorKind::resource_budget_exceeded, code, "render_job",
               message};
}

[[nodiscard]] Error cancel_error(std::string_view code,
                                 std::string_view message) {
  return Error{ExitCode::cancelled, ErrorKind::cancelled, code, "render_job",
               message};
}

} // namespace

RenderJob::RenderJob(std::unique_ptr<const CompiledScene> scene,
                     ResourceBudget budget) noexcept
    : scene_(std::move(scene)), budget_(budget) {
  if (budget_.max_in_flight_frames == 0) {
    budget_.max_in_flight_frames = 2;
  }
}

std::expected<void, Error> RenderJob::enter(RenderJobPhase next,
                                            std::stop_token stop) {
  if (is_terminal_phase(phase_)) {
    return std::unexpected(phase_error("job already terminal"));
  }
  // Cancel wins only before the publish gate.
  if (stop.stop_requested() &&
      (next == RenderJobPhase::preparing || next == RenderJobPhase::rendering ||
       next == RenderJobPhase::finishing ||
       next == RenderJobPhase::publishing)) {
    if (!is_legal_transition(phase_, RenderJobPhase::cancelled) &&
        phase_ != RenderJobPhase::cancelled) {
      return std::unexpected(phase_error("cannot cancel from current phase"));
    }
    phase_ = RenderJobPhase::cancelled;
    return std::unexpected(
        cancel_error(next == RenderJobPhase::publishing
                         ? "render_job.cancelled_before_publish"
                         : "render_job.cancelled",
                     "stop observed before publishing"));
  }
  if (!is_legal_transition(phase_, next)) {
    return std::unexpected(phase_error("illegal render-job phase transition"));
  }
  phase_ = next;
  return {};
}

std::expected<void, Error> RenderJob::fail() noexcept {
  if (phase_ == RenderJobPhase::published) {
    phase_ = RenderJobPhase::published_report_failed;
    return {};
  }
  if (is_terminal_phase(phase_)) {
    return std::unexpected(phase_error("job already terminal"));
  }
  if (phase_ == RenderJobPhase::publishing) {
    // Pre-rename failure while entering publish: no published artifact.
    phase_ = RenderJobPhase::failed;
    return {};
  }
  phase_ = RenderJobPhase::failed;
  return {};
}

std::expected<void, Error> RenderJob::cancel(std::stop_token stop) noexcept {
  (void)stop;
  if (phase_ == RenderJobPhase::publishing || is_published_phase(phase_)) {
    return std::unexpected(
        Error{ExitCode::invalid_input, ErrorKind::invalid_input,
              "render_job.cancel_after_publish_gate", "render_job",
              "cancel cannot undo publishing or published outcomes"});
  }
  if (is_terminal_phase(phase_)) {
    return std::unexpected(phase_error("job already terminal"));
  }
  phase_ = RenderJobPhase::cancelled;
  return {};
}

std::expected<void, Error>
RenderJob::acquire_frame_slot(std::uint64_t frame_bytes) {
  if (budget_.max_in_flight_frames != 0 &&
      in_flight_frames_ >= budget_.max_in_flight_frames) {
    return std::unexpected(budget_error("render_job.in_flight_exhausted",
                                        "in-flight frame budget exhausted"));
  }
  if (budget_.max_accounted_bytes != 0) {
    const auto next = accounted_bytes_ + frame_bytes;
    if (next < accounted_bytes_ || next > budget_.max_accounted_bytes) {
      return std::unexpected(budget_error("render_job.memory_budget_exhausted",
                                          "accounted byte budget exhausted"));
    }
    accounted_bytes_ = next;
  } else {
    accounted_bytes_ += frame_bytes;
  }
  ++in_flight_frames_;
  return {};
}

void RenderJob::release_frame_slot(std::uint64_t frame_bytes) noexcept {
  if (in_flight_frames_ > 0) {
    --in_flight_frames_;
  }
  if (frame_bytes > accounted_bytes_) {
    accounted_bytes_ = 0;
  } else {
    accounted_bytes_ -= frame_bytes;
  }
}

std::expected<void, Error> RenderJob::note_frame_appended() {
  if (budget_.max_output_frames != 0 &&
      frames_committed_ >= budget_.max_output_frames) {
    return std::unexpected(budget_error("render_job.frame_budget_exhausted",
                                        "max_output_frames budget exhausted"));
  }
  ++frames_committed_;
  return {};
}

std::expected<void, Error>
RenderJob::note_publish_outcome(bool published, bool durability_confirmed,
                                bool report_failed) noexcept {
  if (phase_ != RenderJobPhase::publishing &&
      phase_ != RenderJobPhase::published) {
    return std::unexpected(
        phase_error("publish outcome requires publishing phase"));
  }
  if (!published) {
    phase_ = RenderJobPhase::failed;
    return {};
  }
  if (report_failed) {
    phase_ = RenderJobPhase::published_report_failed;
    return {};
  }
  if (!durability_confirmed) {
    phase_ = RenderJobPhase::published_durability_unknown;
    return {};
  }
  phase_ = RenderJobPhase::published;
  return enter(RenderJobPhase::completed);
}

} // namespace instantee::runtime
