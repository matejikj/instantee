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
    // Publishing is past the cancel gate. Outcomes are published* or failed
    // (rename never happened). Cancel is not a legal enter() target here.
    return to == RenderJobPhase::published ||
           to == RenderJobPhase::published_durability_unknown ||
           to == RenderJobPhase::published_report_failed ||
           to == RenderJobPhase::failed;
  case RenderJobPhase::published:
    // Contract: Published → Completed | PublishedDurabilityUnknown |
    // PublishedReportFailed.
    return to == RenderJobPhase::completed ||
           to == RenderJobPhase::published_durability_unknown ||
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

[[nodiscard]] Error default_fail_error() {
  return Error{ExitCode::export_failure, ErrorKind::export_failure,
               "render_job.failed", "render_job", "render job failed"};
}

[[nodiscard]] Error durability_unknown_error() {
  return Error{ExitCode::published_durability_unknown,
               ErrorKind::published_durability_unknown,
               "render_job.published_durability_unknown", "render_job",
               "published output durability was not confirmed"};
}

[[nodiscard]] Error report_failed_error() {
  return Error{ExitCode::report_failure, ErrorKind::report_failure,
               "render_job.published_report_failed", "render_job",
               "post-publish report failed while output remains published"};
}

} // namespace

RenderJob::RenderJob(std::unique_ptr<const CompiledScene> scene,
                     ResourceBudget budget) noexcept
    : scene_(std::move(scene)), budget_(budget) {
  if (budget_.max_in_flight_frames == 0) {
    budget_.max_in_flight_frames = 2;
  }
}

void RenderJob::latch_error(Error error) noexcept {
  if (!first_error_) {
    first_error_ = std::move(error);
  }
}

void RenderJob::note_error(Error error) noexcept {
  latch_error(std::move(error));
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
    auto error = cancel_error(next == RenderJobPhase::publishing
                                  ? "render_job.cancelled_before_publish"
                                  : "render_job.cancelled",
                              "stop observed before publishing");
    latch_error(error);
    phase_ = RenderJobPhase::cancelled;
    return std::unexpected(std::move(error));
  }
  if (!is_legal_transition(phase_, next)) {
    return std::unexpected(phase_error("illegal render-job phase transition"));
  }
  phase_ = next;
  return {};
}

std::expected<void, Error>
RenderJob::fail(std::optional<Error> cause) noexcept {
  if (cause) {
    latch_error(*cause);
  }
  if (phase_ == RenderJobPhase::published) {
    if (!first_error_) {
      latch_error(report_failed_error());
    }
    phase_ = RenderJobPhase::published_report_failed;
    return {};
  }
  if (is_terminal_phase(phase_)) {
    return std::unexpected(phase_error("job already terminal"));
  }
  if (is_published_phase(phase_)) {
    // completed is terminal above; published* handled. Defensive reject so a
    // late fail never claims the published MP4 is absent.
    return std::unexpected(
        Error{ExitCode::invalid_input, ErrorKind::invalid_input,
              "render_job.fail_after_publish", "render_job",
              "fail cannot pretend a published output does not exist"});
  }
  if (!first_error_) {
    latch_error(cause ? *cause : default_fail_error());
  }
  // Pre-rename failure (including while entering publish): no published
  // artifact. Callers that already renamed must use note_publish_outcome.
  phase_ = RenderJobPhase::failed;
  return {};
}

std::expected<void, Error> RenderJob::cancel(std::stop_token stop) noexcept {
  (void)stop;
  if (phase_ == RenderJobPhase::publishing || is_published_phase(phase_)) {
    auto error = Error{ExitCode::invalid_input, ErrorKind::invalid_input,
                       "render_job.cancel_after_publish_gate", "render_job",
                       "cancel cannot undo publishing or published outcomes"};
    latch_error(error);
    return std::unexpected(std::move(error));
  }
  if (is_terminal_phase(phase_)) {
    return std::unexpected(phase_error("job already terminal"));
  }
  auto error = cancel_error("render_job.cancelled",
                            "render job cancelled before publish");
  latch_error(error);
  phase_ = RenderJobPhase::cancelled;
  return {};
}

std::expected<void, Error>
RenderJob::acquire_frame_slot(std::uint64_t frame_bytes) {
  if (budget_.max_in_flight_frames != 0 &&
      in_flight_frames_ >= budget_.max_in_flight_frames) {
    auto error = budget_error("render_job.in_flight_exhausted",
                              "in-flight frame budget exhausted");
    latch_error(error);
    return std::unexpected(std::move(error));
  }
  if (budget_.max_accounted_bytes != 0) {
    const auto next = accounted_bytes_ + frame_bytes;
    if (next < accounted_bytes_ || next > budget_.max_accounted_bytes) {
      auto error = budget_error("render_job.memory_budget_exhausted",
                                "accounted byte budget exhausted");
      latch_error(error);
      return std::unexpected(std::move(error));
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
    auto error = budget_error("render_job.frame_budget_exhausted",
                              "max_output_frames budget exhausted");
    latch_error(error);
    return std::unexpected(std::move(error));
  }
  ++frames_committed_;
  return {};
}

std::expected<void, Error>
RenderJob::note_publish_outcome(bool published, bool durability_confirmed,
                                bool report_failed,
                                std::optional<Error> cause) noexcept {
  if (phase_ != RenderJobPhase::publishing &&
      phase_ != RenderJobPhase::published) {
    return std::unexpected(
        phase_error("publish outcome requires publishing phase"));
  }
  if (cause) {
    latch_error(*cause);
  }
  if (!published) {
    if (!first_error_) {
      latch_error(default_fail_error());
    }
    phase_ = RenderJobPhase::failed;
    return {};
  }
  // Durability outcome outranks a side-report failure: metadata-sync faults
  // set both unknown durability and post_publish_error, and must remain
  // published_durability_unknown rather than published_report_failed.
  if (!durability_confirmed) {
    if (!first_error_) {
      latch_error(cause ? *cause : durability_unknown_error());
    }
    phase_ = RenderJobPhase::published_durability_unknown;
    return {};
  }
  if (report_failed) {
    if (!first_error_) {
      latch_error(cause ? *cause : report_failed_error());
    }
    phase_ = RenderJobPhase::published_report_failed;
    return {};
  }
  phase_ = RenderJobPhase::published;
  return enter(RenderJobPhase::completed);
}

} // namespace instantee::runtime
