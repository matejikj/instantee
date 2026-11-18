#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/lockfile.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
      std::filesystem::temp_directory_path() / "instantee-b02-demo-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::string sha256_hex_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return "present";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: b02_notebook_demo_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto scene_path =
      root /
      "contracts/fixtures/b02-notebook/b02-pythagoras-explainer.scene.json";
  const auto workload_lock =
      root / "contracts/fixtures/b02-notebook/b02-workload.lock.json";
  const auto asset_lock = root / "assets/notebook/notebook.lock.json";

  expect(std::filesystem::exists(scene_path), "scene fixture");
  expect(std::filesystem::exists(workload_lock), "workload lock");
  expect(std::filesystem::exists(asset_lock), "asset lock");
  expect(!sha256_hex_file(scene_path).empty(), "scene readable");

  auto lock = instantee::scene_ir::load_asset_lockfile_file(asset_lock);
  expect(lock.has_value(), "load asset lock");
  if (lock) {
    auto verified = instantee::scene_ir::verify_asset_lockfile(*lock, root);
    expect(verified.has_value(), "offline asset lock verifies");
  }

  instantee::scene_ir::LoadOptions options;
  options.project_root = root;
  options.enforce_uri_sandbox = true;
  auto document = instantee::scene_ir::load_scene_file(scene_path, options);
  expect(document.has_value(), "load B-02 scene");
  if (!document) {
    return EXIT_FAILURE;
  }
  expect(document->duration_frames.value() == 1800, "1800 frames");
  expect(document->canvas_width == 1080 && document->canvas_height == 1920,
         "notebook canvas");
  expect(document->shots.size() == 5, "five notebook layouts");
  expect(document->provenance.style_packages.size() == 1, "style package");
  if (!document->provenance.style_packages.empty()) {
    expect(document->provenance.style_packages.front().id ==
               "scientific-notebook",
           "notebook identity");
  }

  expect(instantee::scene_ir::validate_graph(*document).has_value(),
         "validate graph");
  auto compiled = instantee::compiler::compile_runtime_scene(*document);
  expect(compiled.has_value() && *compiled != nullptr, "compile runtime");
  if (!compiled || !*compiled) {
    return EXIT_FAILURE;
  }

  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return EXIT_FAILURE;
  }
  const auto target = dir / "b02-demo-smoke.mp4";

  auto program =
      instantee::compiler::compile_animation_program(*document, **compiled);
  expect(program.has_value(), "animation program");
  if (!program) {
    return EXIT_FAILURE;
  }
  RenderJob job(std::move(*compiled));
  SceneVideoExportRequest request;
  request.target_path = target;
  // CI smoke: enough frames to prove native assembly without a full 60s run.
  request.frame_count_limit = 60;
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

  auto result = export_scene_video(job, *program, request, factory);
  expect(result.has_value(), "framework scene video export");
  if (!result) {
    std::cerr << result.error().code << ": " << result.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(result->published, "published");
  expect(result->frames_appended == 60, "sixty frames");
  expect(result->phase == RenderJobPhase::completed, "completed");
  expect(std::filesystem::exists(target), "mp4 exists");

  auto inspection = inspect_media(target);
  expect(inspection.has_value(), "inspect media");
  if (inspection) {
    expect(inspection->width == 1080 && inspection->height == 1920, "geometry");
    expect(inspection->video_track_count == 1, "one video track");
    expect(inspection->audio_track_count == 0, "silent");
    expect(inspection->h264, "h264");
    expect(inspection->playable, "playable");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "b02_notebook_demo_test: ok\n";
  return EXIT_SUCCESS;
}
