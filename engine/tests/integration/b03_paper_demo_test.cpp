#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/runtime/frame_preview.hpp>
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
      std::filesystem::temp_directory_path() / "instantee-b03-demo-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: b03_paper_demo_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto scene_path =
      root / "contracts/fixtures/b03-paper/b03-historical-reel.scene.json";
  const auto workload_lock =
      root / "contracts/fixtures/b03-paper/b03-workload.lock.json";
  const auto asset_lock = root / "assets/paper-cinema/paper-cinema.lock.json";

  expect(std::filesystem::exists(scene_path), "scene fixture");
  expect(std::filesystem::exists(workload_lock), "workload lock");
  expect(std::filesystem::exists(asset_lock), "asset lock");

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
  expect(document.has_value(), "load B-03 scene");
  if (!document) {
    if (!document) {
      std::cerr << document.error().code << ": " << document.error().message
                << '\n';
    }
    return EXIT_FAILURE;
  }
  expect(document->duration_frames.value() == 1800, "1800 frames");
  expect(document->canvas_width == 1080 && document->canvas_height == 1920,
         "paper canvas");
  expect(document->shots.size() == 5, "five templates");
  expect(document->provenance.style_packages.size() == 1, "style package");
  if (!document->provenance.style_packages.empty()) {
    expect(document->provenance.style_packages.front().id == "paper-cinema",
           "paper-cinema identity");
  }
  expect(!document->rigs.empty(), "historical puppet rig provenance");

  {
    auto graph = instantee::scene_ir::validate_graph(*document);
    expect(graph.has_value(), "validate graph");
    if (!graph) {
      std::cerr << graph.error().code << ": " << graph.error().message;
      if (graph.error().context.json_path) {
        std::cerr << " @ " << *graph.error().context.json_path;
      }
      std::cerr << '\n';
    }
  }
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
  const auto target = dir / "b03-demo-smoke.mp4";

  auto program =
      instantee::compiler::compile_animation_program(*document, **compiled);
  expect(program.has_value(), "animation program");
  if (!program) {
    return EXIT_FAILURE;
  }

  // Figure opacity should be on during reveal and off after its fade window.
  {
    using instantee::core::FrameIndex;
    using instantee::runtime::preview_frame_at;
    const auto &scene = **compiled;
    auto mid = preview_frame_at(scene, *program, FrameIndex{120});
    auto after = preview_frame_at(scene, *program, FrameIndex{400});
    expect(mid.has_value() && after.has_value(), "opacity lifecycle frames");
    std::optional<std::uint32_t> figure_index;
    for (std::uint32_t i = 0; i < scene.node_ids().size(); ++i) {
      if (scene.node_ids()[i] == "figure_torso") {
        figure_index = i;
        break;
      }
    }
    expect(figure_index.has_value(), "figure_torso node");
    if (figure_index && mid && after) {
      expect(mid->state.nodes[*figure_index].local.opacity >= 0.99,
             "figure opaque mid reveal");
      expect(after->state.nodes[*figure_index].local.opacity <= 0.01,
             "figure faded after reveal");
    }
  }

  RenderJob job(std::move(*compiled));
  SceneVideoExportRequest request;
  request.target_path = target;
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
  std::cout << "b03_paper_demo_test: ok\n";
  return EXIT_SUCCESS;
}
