#include <instantee/compiler/compile.hpp>
#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

using instantee::compiler::compile_authoring_with_report;
using instantee::compiler::CompileOptions;
using instantee::compiler::scene_document_to_json;
using instantee::export_apple::AppleVideoSink;
using instantee::runtime::AnimationProgram;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::scene_ir::load_authoring_json;
using instantee::scene_ir::LoadOptions;
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
      std::filesystem::temp_directory_path() / "instantee-cds-sdk-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: cds_sdk_e2e_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto authoring_path =
      root /
      "contracts/fixtures/authoring/positive/cds-sdk-intents.authoring.json";

  // Fixture mirrors TypeScript createFilm({ seed, styleOverride, intent }).
  std::ifstream input(authoring_path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string authoring_text = buffer.str();
  LoadOptions load_options;
  auto authoring = load_authoring_json(authoring_text, load_options);
  expect(authoring.has_value(), "load SDK-shaped authoring");
  if (!authoring) {
    return EXIT_FAILURE;
  }
  expect(authoring->seed == 99, "explicit seed");
  expect(!authoring->shots.empty() &&
             authoring->shots.front().style_override.has_value() &&
             authoring->shots.front().intent.has_value(),
         "typed intent + per-shot override present");

  CompileOptions options;
  auto compiled = compile_authoring_with_report(*authoring, options);
  expect(compiled.has_value(), "CDS compile with direction report");
  if (!compiled) {
    std::cerr << "compile error: " << compiled.error().code << " — "
              << compiled.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(!compiled->document.animation_tracks.empty(), "tracks emitted");
  expect(!compiled->direction_report.intents().empty(), "intent decisions");
  expect(!compiled->direction_report.channels().empty(), "channel decisions");
  const std::string snapshot = compiled->direction_report.to_snapshot();
  expect(snapshot.find("direction_report/1") == 0, "report header");
  expect(snapshot.find("shot_override") != std::string::npos ||
             snapshot.find("style_bible") != std::string::npos,
         "report names source");
  auto json = scene_document_to_json(compiled->document);
  expect(json.has_value() && json->find("\"intent\"") == std::string::npos,
         "no unresolved intent in Scene IR");
  expect(instantee::scene_ir::validate_graph(compiled->document).has_value(),
         "validate IR graph");

  auto runtime = instantee::compiler::compile_runtime_scene(compiled->document);
  expect(runtime.has_value(), "compile runtime scene");
  if (!runtime) {
    return EXIT_FAILURE;
  }

  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return EXIT_FAILURE;
  }
  const auto target = dir / "cds_sdk.mp4";
  const auto report_path = dir / "direction_report.txt";
  {
    std::ofstream report_out(report_path, std::ios::binary | std::ios::trunc);
    report_out << snapshot;
    expect(static_cast<bool>(report_out), "write direction report");
  }

  RenderJob job(std::move(*runtime));
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

  auto exported = export_scene_video(job, program, request, factory);
  expect(exported.has_value(), "export MP4 from CDS Scene IR");
  if (!exported) {
    std::cerr << "export error: " << exported.error().code << " — "
              << exported.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(exported->published, "published");
  expect(exported->frames_appended == 4, "four frames");
  expect(exported->phase == RenderJobPhase::completed, "completed");
  expect(std::filesystem::exists(target), "mp4 exists");

  auto inspection = inspect_media(target);
  expect(inspection.has_value(), "inspect media");
  if (inspection) {
    expect(inspection->width == 64 && inspection->height == 64, "geometry");
    expect(inspection->h264, "h264");
    expect(inspection->playable, "playable");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
