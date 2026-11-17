#include <instantee/export/scene_video_export.hpp>

#include <instantee/export_core/rec709_handoff.hpp>
#include <instantee/runtime/frame_preview.hpp>

#include <algorithm>
#include <utility>

namespace instantee::video_export {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;
using export_core::CpuFrameBridge;
using export_core::DurabilityState;
using export_core::OutputTransaction;
using export_core::OutputTransactionConfig;
using export_core::PublishResult;
using export_core::VideoSettings;
using preview::rasterize_preview_frame;
using runtime::preview_frame_at;
using runtime::RenderJobPhase;

[[nodiscard]] Error make_error(ExitCode exit_code, ErrorKind kind,
                               std::string_view code,
                               std::string_view message) {
  return Error{exit_code, kind, code, "scene_video_export", message};
}

[[nodiscard]] std::uint64_t
frame_rgba_bytes(const runtime::CompiledScene &scene) noexcept {
  return static_cast<std::uint64_t>(scene.canvas_width()) *
         static_cast<std::uint64_t>(scene.canvas_height()) * 4ULL;
}

[[nodiscard]] SceneVideoExportResult
make_result(const runtime::RenderJob &job, std::uint64_t frames,
            const PublishResult *publish = nullptr) {
  SceneVideoExportResult out;
  out.phase = job.phase();
  out.frames_appended = frames;
  if (publish != nullptr) {
    out.published = publish->published;
    out.durability = publish->durability;
    out.post_publish_error = publish->post_publish_error;
  }
  return out;
}

} // namespace

std::expected<SceneVideoExportResult, Error>
export_scene_video(runtime::RenderJob &job,
                   const runtime::AnimationProgram &program,
                   const SceneVideoExportRequest &request,
                   const VideoSinkFactory &make_sink, std::stop_token stop) {
  if (!job.has_scene()) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "missing_scene",
                                      "RenderJob requires a compiled scene"));
  }
  if (!make_sink) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "missing_sink",
                                      "VideoSink factory is required"));
  }
  if (request.target_path.empty()) {
    return std::unexpected(make_error(ExitCode::invalid_input,
                                      ErrorKind::invalid_input, "empty_target",
                                      "target_path must be non-empty"));
  }

  if (auto entered = job.enter(RenderJobPhase::preparing, stop); !entered) {
    return std::unexpected(entered.error());
  }

  OutputTransactionConfig txn_config;
  txn_config.target_path = request.target_path;
  txn_config.mode = request.publish_mode;
  txn_config.durability = request.durability;

  auto txn = OutputTransaction::begin(std::move(txn_config));
  if (!txn) {
    (void)job.fail();
    return std::unexpected(txn.error());
  }

  auto sink = make_sink(txn->staging_file());
  if (!sink) {
    (void)job.fail();
    return std::unexpected(sink.error());
  }

  const runtime::CompiledScene &scene = job.scene();
  VideoSettings settings{.width = scene.canvas_width(),
                         .height = scene.canvas_height(),
                         .frame_rate = scene.frame_rate()};
  if (auto begun = (*sink)->begin(settings); !begun) {
    (void)job.fail();
    return std::unexpected(begun.error());
  }

  CpuFrameBridge bridge(std::move(*sink));

  if (auto entered = job.enter(RenderJobPhase::rendering, stop); !entered) {
    bridge.sink().cancel();
    return std::unexpected(entered.error());
  }

  const std::uint64_t scene_frames = scene.duration_frames().value();
  std::uint64_t limit = scene_frames;
  if (request.frame_count_limit.has_value()) {
    limit = std::min(limit, *request.frame_count_limit);
  }

  const std::uint64_t slot_bytes = frame_rgba_bytes(scene);
  auto clock = core::FrameClock::create(scene.frame_rate());
  if (!clock) {
    bridge.sink().cancel();
    (void)job.fail();
    return std::unexpected(clock.error());
  }

  for (std::uint64_t f = 0; f < limit; ++f) {
    if (stop.stop_requested()) {
      bridge.sink().cancel();
      (void)job.cancel(stop);
      return std::unexpected(make_error(ExitCode::cancelled,
                                        ErrorKind::cancelled, "cancelled",
                                        "stop observed during rendering"));
    }

    // Enforce max_output_frames before producing the next frame so a budget
    // exceed never partially appends beyond the declared ceiling.
    if (job.budget().max_output_frames != 0 &&
        job.frames_committed() >= job.budget().max_output_frames) {
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(make_error(ExitCode::resource_budget_exceeded,
                                        ErrorKind::resource_budget_exceeded,
                                        "render_job.frame_budget_exhausted",
                                        "max_output_frames budget exhausted"));
    }

    if (auto slot = job.acquire_frame_slot(slot_bytes); !slot) {
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(slot.error());
    }

    const core::FrameIndex at{f};
    auto preview = preview_frame_at(scene, program, at);
    if (!preview) {
      job.release_frame_slot(slot_bytes);
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(preview.error());
    }

    auto image = rasterize_preview_frame(scene, preview->state, request.raster);
    if (!image) {
      job.release_frame_slot(slot_bytes);
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(image.error());
    }

    auto pts = clock->pts(at);
    if (!pts) {
      job.release_frame_slot(slot_bytes);
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(pts.error());
    }

    auto published = export_core::publish_rec709_rgba8(
        bridge, image->width_px, image->height_px, image->rgba, at, *pts, stop);
    if (!published) {
      job.release_frame_slot(slot_bytes);
      bridge.sink().cancel();
      if (published.error().kind == ErrorKind::cancelled) {
        (void)job.cancel(stop);
      } else {
        (void)job.fail();
      }
      return std::unexpected(published.error());
    }

    if (auto counted = job.note_frame_appended(); !counted) {
      job.release_frame_slot(slot_bytes);
      bridge.sink().cancel();
      (void)job.fail();
      return std::unexpected(counted.error());
    }
    job.release_frame_slot(slot_bytes);
  }

  if (auto entered = job.enter(RenderJobPhase::finishing, stop); !entered) {
    bridge.sink().cancel();
    return std::unexpected(entered.error());
  }

  auto summary = bridge.sink().finish();
  if (!summary) {
    (void)job.fail();
    return std::unexpected(summary.error());
  }

  // Observe stop before the irrevocable publish gate.
  if (auto entered = job.enter(RenderJobPhase::publishing, stop); !entered) {
    return std::unexpected(entered.error());
  }

  auto publish = txn->commit(stop);
  if (!publish) {
    (void)job.fail();
    return std::unexpected(publish.error());
  }

  const bool durability_ok =
      publish->durability == DurabilityState::confirmed ||
      publish->durability == DurabilityState::not_applicable;
  const bool report_failed = publish->post_publish_error.has_value();
  if (auto noted = job.note_publish_outcome(publish->published, durability_ok,
                                            report_failed);
      !noted) {
    return std::unexpected(noted.error());
  }

  return make_result(job, summary->frames_appended, &*publish);
}

} // namespace instantee::video_export
