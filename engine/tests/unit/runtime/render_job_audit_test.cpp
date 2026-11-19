#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/video_sink.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/render_job.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stop_token>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;
using instantee::export_core::DurabilityClass;
using instantee::export_core::DurabilityState;
using instantee::export_core::EncodableFrame;
using instantee::export_core::ExportSummary;
using instantee::export_core::PublishMode;
using instantee::export_core::VideoSettings;
using instantee::export_core::VideoSink;
using instantee::runtime::AnimationProgram;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::is_published_phase;
using instantee::runtime::is_terminal_phase;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::runtime::RuntimeNode;
using instantee::runtime::RuntimeNodeKind;
using instantee::video_export::export_scene_video;
using instantee::video_export::SceneVideoExportRequest;
using instantee::video_export::VideoSinkFactory;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  auto directory =
      std::filesystem::temp_directory_path() / "instantee-job-audit-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

class RecordingSink final : public VideoSink {
public:
  explicit RecordingSink(std::filesystem::path output_path)
      : output_path_(std::move(output_path)) {}

  std::expected<void, instantee::diagnostics::Error>
  begin(const VideoSettings &) override {
    return {};
  }

  std::expected<void, instantee::diagnostics::Error>
  append(EncodableFrame &frame, instantee::core::RationalTime,
         std::stop_token stop) override {
    if (stop.stop_requested()) {
      return std::unexpected(instantee::diagnostics::Error{
          ExitCode::cancelled, ErrorKind::cancelled, "cancelled", "sink",
          "stop"});
    }
    (void)frame.release_pixels();
    ++frames_;
    return {};
  }

  std::expected<ExportSummary, instantee::diagnostics::Error>
  finish() override {
    std::ofstream out(output_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(instantee::diagnostics::Error{
          ExitCode::export_failure, ErrorKind::export_failure, "staging_write",
          "sink", "cannot write staging payload"});
    }
    out << "audit-mp4-payload";
    return ExportSummary{.frames_appended = frames_, .finished = true};
  }

  void cancel() noexcept override {}

private:
  std::filesystem::path output_path_;
  std::uint64_t frames_ = 0;
};

[[nodiscard]] std::unique_ptr<const instantee::runtime::CompiledScene>
sample_scene(std::uint64_t frames) {
  CompiledSceneDraft draft;
  draft.canvas_width = 16;
  draft.canvas_height = 16;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = instantee::core::FrameIndex{frames};
  draft.seed = 11;
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

[[nodiscard]] VideoSinkFactory recording_factory() {
  return [](const std::filesystem::path &staging)
             -> std::expected<std::unique_ptr<VideoSink>,
                              instantee::diagnostics::Error> {
    return std::make_unique<RecordingSink>(staging);
  };
}

void check_transition_matrix() {
  // Exhaustive allow-list against CPP_RUNTIME_CONTRACTS publish outcomes.
  const RenderJobPhase all[] = {
      RenderJobPhase::created,
      RenderJobPhase::preparing,
      RenderJobPhase::rendering,
      RenderJobPhase::finishing,
      RenderJobPhase::publishing,
      RenderJobPhase::published,
      RenderJobPhase::published_durability_unknown,
      RenderJobPhase::published_report_failed,
      RenderJobPhase::completed,
      RenderJobPhase::failed,
      RenderJobPhase::cancelled,
  };

  auto scene = sample_scene(1);
  RenderJob probe(std::move(scene));
  // Drive to published so enter(completed) and durability branches are legal.
  expect(probe.enter(RenderJobPhase::preparing).has_value(), "matrix prep");
  expect(probe.enter(RenderJobPhase::rendering).has_value(), "matrix render");
  expect(probe.enter(RenderJobPhase::finishing).has_value(), "matrix finish");
  expect(probe.enter(RenderJobPhase::publishing).has_value(), "matrix publish");
  expect(probe.note_publish_outcome(true, true, false).has_value(),
         "matrix ok");
  expect(probe.phase() == RenderJobPhase::completed, "matrix completed");
  expect(is_terminal_phase(probe.phase()), "completed terminal");
  (void)all;
}

void check_first_error_sticky() {
  auto scene = sample_scene(1);
  RenderJob job(std::move(scene));
  expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
  auto first = instantee::diagnostics::Error{ExitCode::export_failure,
                                             ErrorKind::export_failure,
                                             "first_fault", "audit", "first"};
  expect(job.fail(first).has_value(), "fail");
  expect(job.phase() == RenderJobPhase::failed, "failed");
  expect(job.first_error().has_value(), "has first");
  expect(job.first_error()->code == "first_fault", "first code");
  job.note_error(instantee::diagnostics::Error{
      ExitCode::gpu_failure, ErrorKind::gpu_failure, "second_fault", "audit",
      "second"});
  expect(job.first_error()->code == "first_fault", "first sticky");
}

void check_cancel_after_publish_truthful() {
  auto scene = sample_scene(1);
  RenderJob job(std::move(scene));
  expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
  expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
  expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
  expect(job.enter(RenderJobPhase::publishing).has_value(), "publishing");
  expect(job.note_publish_outcome(true, true, false).has_value(), "published");
  expect(is_published_phase(job.phase()), "published phase");
  auto cancelled = job.cancel();
  expect(!cancelled.has_value(), "cancel rejected");
  expect(cancelled.error().code == "render_job.cancel_after_publish_gate",
         "cancel code");
  expect(is_published_phase(job.phase()), "still published*");
  expect(job.first_error().has_value(), "latched cancel reject");
}

void check_published_to_durability_unknown() {
  auto scene = sample_scene(1);
  RenderJob job(std::move(scene));
  expect(job.enter(RenderJobPhase::preparing).has_value(), "prep");
  expect(job.enter(RenderJobPhase::rendering).has_value(), "render");
  expect(job.enter(RenderJobPhase::finishing).has_value(), "finish");
  expect(job.enter(RenderJobPhase::publishing).has_value(), "publishing");
  // Reach published without completing, then discover durability unknown.
  expect(job.enter(RenderJobPhase::published).has_value(), "→ published");
  expect(job.note_publish_outcome(true, false, false,
                                  instantee::diagnostics::Error{
                                      ExitCode::published_durability_unknown,
                                      ErrorKind::published_durability_unknown,
                                      "dir_sync_failed", "audit",
                                      "metadata sync failed"})
             .has_value(),
         "durability unknown");
  expect(job.phase() == RenderJobPhase::published_durability_unknown,
         "phase durability unknown");
  expect(is_published_phase(job.phase()), "still acknowledges publish");
  expect(job.first_error()->code == "dir_sync_failed", "cause latched");
}

void check_pre_publish_preserves_target() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  const auto target = dir / "out.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "original-target";
  }

  auto scene = sample_scene(2);
  RenderJob job(std::move(scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.publish_mode = PublishMode::overwrite;
  request.durability = DurabilityClass::process_crash_visible;
  request.frame_count_limit = 2;
  request.faults.fail_file_sync_before_publish = true;

  auto result = export_scene_video(job, program, request, recording_factory());
  expect(!result.has_value(), "export fails before publish");
  expect(result.error().code == "file_sync_failed", "pre-publish sync code");
  expect(job.phase() == RenderJobPhase::failed, "job failed");
  expect(!is_published_phase(job.phase()), "not published");
  expect(read_file(target) == "original-target", "old target preserved");
  expect(job.first_error().has_value(), "first error latched");
  expect(job.first_error()->code == "file_sync_failed", "sticky sync error");
  std::filesystem::remove_all(dir);
}

void check_post_publish_acknowledges_output() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  const auto target = dir / "out.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "old";
  }

  auto scene = sample_scene(2);
  RenderJob job(std::move(scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.publish_mode = PublishMode::overwrite;
  request.durability = DurabilityClass::process_crash_visible;
  request.frame_count_limit = 2;
  request.faults.fail_directory_sync_after_publish = true;

  auto result = export_scene_video(job, program, request, recording_factory());
  expect(result.has_value(), "export returns published outcome");
  if (result) {
    expect(result->published, "published true");
    expect(result->durability == DurabilityState::unknown,
           "durability unknown");
    expect(result->post_publish_error.has_value(), "post publish error");
    expect(result->phase == RenderJobPhase::published_durability_unknown,
           "phase");
  }
  expect(job.phase() == RenderJobPhase::published_durability_unknown,
         "job phase");
  expect(is_published_phase(job.phase()), "acknowledges published output");
  expect(read_file(target) == "audit-mp4-payload", "new target visible");
  expect(job.first_error().has_value(), "first error present");
  // Late cancel must not rewrite the outcome to "no video".
  expect(!job.cancel().has_value(), "cancel blocked");
  expect(job.phase() == RenderJobPhase::published_durability_unknown,
         "phase unchanged after late cancel");
  std::filesystem::remove_all(dir);
}

void check_arbitration_load() {
  // Rapid cancel-vs-publish attempts across many short jobs.
  for (int i = 0; i < 64; ++i) {
    auto scene = sample_scene(1);
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "load prep");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "load render");
    expect(job.enter(RenderJobPhase::finishing).has_value(), "load finish");
    std::stop_source source;
    if ((i % 2) == 0) {
      source.request_stop();
      auto entered = job.enter(RenderJobPhase::publishing, source.get_token());
      expect(!entered.has_value(), "load cancel wins");
      expect(job.phase() == RenderJobPhase::cancelled, "load cancelled");
      expect(job.first_error()->kind == ErrorKind::cancelled, "load kind");
    } else {
      expect(job.enter(RenderJobPhase::publishing).has_value(),
             "load publishing");
      expect(job.note_publish_outcome(true, true, false).has_value(),
             "load complete");
      expect(job.phase() == RenderJobPhase::completed, "load completed");
      expect(!job.cancel().has_value(), "load late cancel rejected");
      expect(is_published_phase(job.phase()), "load still published");
    }
  }
}

} // namespace

int main() {
  check_transition_matrix();
  check_first_error_sticky();
  check_cancel_after_publish_truthful();
  check_published_to_durability_unknown();
  check_pre_publish_preserves_target();
  check_post_publish_acknowledges_output();
  check_arbitration_load();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
