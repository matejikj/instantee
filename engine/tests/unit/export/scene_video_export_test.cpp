#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_core/video_sink.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stop_token>
#include <unistd.h>
#include <vector>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::export_core::EncodableFrame;
using instantee::export_core::ExportSummary;
using instantee::export_core::VideoSettings;
using instantee::export_core::VideoSink;
using instantee::runtime::AnimationProgram;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::runtime::ResourceBudget;
using instantee::runtime::RuntimeNode;
using instantee::runtime::RuntimeNodeKind;
using instantee::video_export::export_scene_video;
using instantee::video_export::SceneVideoExportRequest;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class RecordingSink final : public VideoSink {
public:
  explicit RecordingSink(std::filesystem::path output_path)
      : output_path_(std::move(output_path)) {}

  std::expected<void, instantee::diagnostics::Error>
  begin(const VideoSettings &settings) override {
    settings_ = settings;
    begun_ = true;
    return {};
  }

  std::expected<void, instantee::diagnostics::Error>
  append(EncodableFrame &frame, instantee::core::RationalTime,
         std::stop_token stop) override {
    if (stop.stop_requested()) {
      return std::unexpected(instantee::diagnostics::Error{
          instantee::diagnostics::ExitCode::cancelled, ErrorKind::cancelled,
          "cancelled", "sink", "stop"});
    }
    frames_.push_back(frame.release_pixels());
    return {};
  }

  std::expected<ExportSummary, instantee::diagnostics::Error>
  finish() override {
    // OutputTransaction requires a staging regular file before publish.
    std::ofstream out(output_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(instantee::diagnostics::Error{
          instantee::diagnostics::ExitCode::export_failure,
          ErrorKind::export_failure, "staging_write", "sink",
          "cannot write staging payload"});
    }
    out << "fake-mp4-payload";
    finished_ = true;
    return ExportSummary{.frames_appended = frames_.size(), .finished = true};
  }

  void cancel() noexcept override { cancelled_ = true; }

  [[nodiscard]] std::size_t frame_count() const noexcept {
    return frames_.size();
  }
  [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }

private:
  std::filesystem::path output_path_;
  VideoSettings settings_{};
  std::vector<std::vector<std::byte>> frames_{};
  bool begun_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
};

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-export-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::unique_ptr<const instantee::runtime::CompiledScene>
sample_scene(std::uint64_t frames) {
  CompiledSceneDraft draft;
  draft.canvas_width = 32;
  draft.canvas_height = 32;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = instantee::core::FrameIndex{frames};
  draft.seed = 3;
  draft.node_ids = {"g0"};
  RuntimeNode node;
  node.kind = RuntimeNodeKind::geometry;
  node.visible = true;
  draft.nodes = {node};
  auto scene = make_compiled_scene(std::move(draft));
  if (!scene) {
    return nullptr;
  }
  return std::move(*scene);
}

} // namespace

int main() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return EXIT_FAILURE;
  }

  auto factory = [](const std::filesystem::path &staging)
      -> std::expected<std::unique_ptr<VideoSink>,
                       instantee::diagnostics::Error> {
    return std::make_unique<RecordingSink>(staging);
  };

  {
    auto scene = sample_scene(3);
    expect(scene != nullptr, "scene");
    RenderJob job(std::move(scene));
    AnimationProgram program;
    SceneVideoExportRequest request;
    request.target_path = dir / "ok.mp4";
    request.frame_count_limit = 3;
    request.durability =
        instantee::export_core::DurabilityClass::process_crash_visible;

    std::uint64_t progress_calls = 0;
    std::uint64_t last_completed = 0;
    std::uint64_t last_total = 0;
    request.on_progress = [&](std::uint64_t completed, std::uint64_t total) {
      ++progress_calls;
      last_completed = completed;
      last_total = total;
    };

    auto result = export_scene_video(job, program, request, factory);
    expect(result.has_value(), "export ok");
    if (result) {
      expect(result->frames_appended == 3, "three frames");
      expect(result->published, "published");
      expect(result->phase == RenderJobPhase::completed, "completed");
      expect(std::filesystem::exists(request.target_path), "target exists");
      expect(progress_calls == 3, "progress once per frame");
      expect(last_completed == 3 && last_total == 3, "progress totals");
    }
  }

  {
    auto scene = sample_scene(4);
    ResourceBudget budget;
    budget.max_output_frames = 2;
    RenderJob job(std::move(scene), budget);
    AnimationProgram program;
    SceneVideoExportRequest request;
    request.target_path = dir / "budget.mp4";
    request.frame_count_limit = 4;
    request.durability =
        instantee::export_core::DurabilityClass::process_crash_visible;

    auto result = export_scene_video(job, program, request, factory);
    expect(!result.has_value(), "budget exceed");
    expect(result.error().kind == ErrorKind::resource_budget_exceeded,
           "budget kind");
    expect(job.phase() == RenderJobPhase::failed, "failed phase");
    expect(!std::filesystem::exists(request.target_path),
           "no target on budget fail");
  }

  {
    auto scene = sample_scene(3);
    RenderJob job(std::move(scene));
    AnimationProgram program;
    SceneVideoExportRequest request;
    request.target_path = dir / "cancel.mp4";
    request.frame_count_limit = 3;
    request.durability =
        instantee::export_core::DurabilityClass::process_crash_visible;

    std::stop_source source;
    source.request_stop();
    auto result =
        export_scene_video(job, program, request, factory, source.get_token());
    expect(!result.has_value(), "cancelled export");
    expect(result.error().kind == ErrorKind::cancelled, "cancel kind");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled phase");
    expect(!std::filesystem::exists(request.target_path),
           "no target on cancel");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
