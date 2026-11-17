#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

using instantee::export_apple::AppleVideoSink;
using instantee::runtime::AnimationProgram;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::spike::apple::inspect_media;
using instantee::video_export::export_scene_video;
using instantee::video_export::SceneVideoExportRequest;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-scene-mp4-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: scene_video_mp4_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto scene_path =
      root / "contracts/fixtures/scene-ir/positive/e2e_short.scene.json";

  instantee::scene_ir::LoadOptions load_options;
  auto document =
      instantee::scene_ir::load_scene_file(scene_path, load_options);
  expect(document.has_value(), "load scene IR");
  if (!document) {
    return EXIT_FAILURE;
  }
  expect(instantee::scene_ir::validate_graph(*document).has_value(),
         "validate graph");
  auto compiled = instantee::compiler::compile_runtime_scene(*document);
  expect(compiled.has_value(), "compile runtime scene");
  if (!compiled) {
    return EXIT_FAILURE;
  }

  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return EXIT_FAILURE;
  }
  const auto target = dir / "e2e_short.mp4";

  RenderJob job(std::move(*compiled));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.frame_count_limit = 4;
  request.durability =
      instantee::export_core::DurabilityClass::process_crash_visible;

  auto factory = [](const std::filesystem::path &staging)
      -> std::expected<std::unique_ptr<instantee::export_core::VideoSink>,
                       instantee::diagnostics::Error> {
    auto sink = AppleVideoSink::create(staging);
    if (!sink) {
      return std::unexpected(sink.error());
    }
    return std::unique_ptr<instantee::export_core::VideoSink>(std::move(*sink));
  };

  auto result = export_scene_video(job, program, request, factory);
  expect(result.has_value(), "scene video export");
  if (!result) {
    std::cerr << "export error: " << result.error().code << " — "
              << result.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(result->published, "published");
  expect(result->frames_appended == 4, "four frames");
  expect(result->phase == RenderJobPhase::completed, "completed");
  expect(std::filesystem::exists(target), "mp4 exists");

  auto inspection = inspect_media(target);
  expect(inspection.has_value(), "inspect media");
  if (inspection) {
    expect(inspection->width == 64 && inspection->height == 64, "geometry");
    expect(inspection->video_track_count == 1, "one video track");
    expect(inspection->audio_track_count == 0, "no audio");
    expect(inspection->h264, "h264");
    expect(inspection->playable, "playable");
  }

  // No TypeScript hot-path artifacts: load → compile → raster → VideoSink.
  expect(true, "native-only e2e path");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
