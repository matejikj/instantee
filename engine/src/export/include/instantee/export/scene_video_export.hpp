#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/video_sink.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/frame_evaluator.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

// Namespace avoids the C++ `export` keyword while matching the export module.
namespace instantee::video_export {

// Factory builds a VideoSink aimed at the OutputTransaction staging file.
// begin() is invoked by the export loop after settings are known.
using VideoSinkFactory = std::function<
    std::expected<std::unique_ptr<export_core::VideoSink>, diagnostics::Error>(
        const std::filesystem::path &staging_file)>;

// Invoked after each successfully appended frame. `completed` is 1-based;
// `total` is the planned append count for this request. Must not throw.
using ProgressCallback =
    std::function<void(std::uint64_t completed, std::uint64_t total)>;

struct SceneVideoExportRequest {
  std::filesystem::path target_path;
  export_core::PublishMode publish_mode = export_core::PublishMode::no_replace;
  export_core::DurabilityClass durability =
      export_core::DurabilityClass::durable_local;
  // Optional hard stop before scene duration (tests / short clips).
  std::optional<std::uint64_t> frame_count_limit{};
  preview::FramePngRequest raster{};
  ProgressCallback on_progress{};
};

struct SceneVideoExportResult {
  runtime::RenderJobPhase phase = runtime::RenderJobPhase::created;
  std::uint64_t frames_appended = 0;
  bool published = false;
  export_core::DurabilityState durability =
      export_core::DurabilityState::not_applicable;
  std::optional<diagnostics::Error> post_publish_error{};
};

// Drives RenderJob phases through evaluate → preview raster → VideoSink →
// OutputTransaction. Cancel observed before Publishing wins; after the publish
// gate the result stays published*. Declared job budgets are enforced per
// frame.
[[nodiscard]] std::expected<SceneVideoExportResult, diagnostics::Error>
export_scene_video(runtime::RenderJob &job,
                   const runtime::AnimationProgram &program,
                   const SceneVideoExportRequest &request,
                   const VideoSinkFactory &make_sink,
                   std::stop_token stop = {});

} // namespace instantee::video_export
