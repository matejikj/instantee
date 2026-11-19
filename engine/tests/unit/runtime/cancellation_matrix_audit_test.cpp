#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/video_sink.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_eval_scheduler.hpp>
#include <instantee/runtime/render_job.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;
using instantee::export_core::DurabilityClass;
using instantee::export_core::EncodableFrame;
using instantee::export_core::ExportSummary;
using instantee::export_core::PublishMode;
using instantee::export_core::VideoSettings;
using instantee::export_core::VideoSink;
using instantee::runtime::AnimationProgram;
using instantee::runtime::BoundedQueue;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::FrameEvalScheduler;
using instantee::runtime::is_published_phase;
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
      std::filesystem::temp_directory_path() / "instantee-cancel-matrix-XXXXXX";
  std::string template_path = directory.string();
  if (::mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::unique_ptr<const instantee::runtime::CompiledScene>
sample_scene(std::uint64_t frames) {
  CompiledSceneDraft draft;
  draft.canvas_width = 16;
  draft.canvas_height = 16;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = instantee::core::FrameIndex{frames};
  draft.seed = 19;
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

// Shared completion context outlives the sink owner so a late cancel callback
// cannot use-after-free the job stack frame.
struct LateCallbackContext {
  std::mutex mutex;
  std::condition_variable cv;
  bool cancel_seen = false;
  bool late_finished = false;
  std::atomic_bool touched{false};
};

struct AuditSinkOptions {
  std::shared_ptr<LateCallbackContext> late{};
  std::stop_source *request_stop_on_finish = nullptr;
};

class AuditSink final : public VideoSink {
public:
  AuditSink(std::filesystem::path output_path, AuditSinkOptions options)
      : output_path_(std::move(output_path)), options_(std::move(options)) {}

  ~AuditSink() override {
    if (late_thread_.joinable()) {
      late_thread_.join();
    }
  }

  std::expected<void, instantee::diagnostics::Error>
  begin(const VideoSettings &) override {
    return {};
  }

  std::expected<void, instantee::diagnostics::Error>
  append(EncodableFrame &frame, instantee::core::RationalTime,
         std::stop_token stop) override {
    if (stop.stop_requested() || cancelled_.load(std::memory_order_acquire)) {
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
    if (cancelled_.load(std::memory_order_acquire)) {
      return std::unexpected(instantee::diagnostics::Error{
          ExitCode::cancelled, ErrorKind::cancelled, "cancelled", "sink",
          "finish after cancel"});
    }
    std::ofstream out(output_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
      return std::unexpected(instantee::diagnostics::Error{
          ExitCode::export_failure, ErrorKind::export_failure, "staging_write",
          "sink", "cannot write staging payload"});
    }
    out << "cancel-matrix-payload";
    if (options_.request_stop_on_finish != nullptr) {
      options_.request_stop_on_finish->request_stop();
    }
    return ExportSummary{.frames_appended = frames_, .finished = true};
  }

  void cancel() noexcept override {
    cancelled_.store(true, std::memory_order_release);
    if (!options_.late || late_spawned_.exchange(true)) {
      return;
    }
    {
      std::lock_guard lock(options_.late->mutex);
      options_.late->cancel_seen = true;
      options_.late->cv.notify_all();
    }
    // Deferred "native" completion: touches only the shared context.
    late_thread_ = std::thread([ctx = options_.late] {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      ctx->touched.store(true, std::memory_order_release);
      std::lock_guard lock(ctx->mutex);
      ctx->late_finished = true;
      ctx->cv.notify_all();
    });
  }

private:
  std::filesystem::path output_path_;
  AuditSinkOptions options_{};
  std::uint64_t frames_ = 0;
  std::atomic_bool cancelled_{false};
  std::atomic_bool late_spawned_{false};
  std::thread late_thread_;
};

[[nodiscard]] VideoSinkFactory make_factory(AuditSinkOptions options) {
  return [options](const std::filesystem::path &staging)
             -> std::expected<std::unique_ptr<VideoSink>,
                              instantee::diagnostics::Error> {
    return std::make_unique<AuditSink>(staging, options);
  };
}

void check_stop_every_pre_publish_phase() {
  const RenderJobPhase stops_at[] = {
      RenderJobPhase::preparing,
      RenderJobPhase::rendering,
      RenderJobPhase::finishing,
      RenderJobPhase::publishing,
  };
  for (const RenderJobPhase gate : stops_at) {
    auto scene = sample_scene(1);
    RenderJob job(std::move(scene));
    std::stop_source stop;
    stop.request_stop();
    auto entered = job.enter(gate, stop.get_token());
    expect(!entered.has_value(), "stop rejects enter");
    expect(entered.error().kind == ErrorKind::cancelled, "cancel kind");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled phase");
    expect(job.first_error().has_value(), "first error latched");
  }

  // Explicit cancel() from each non-terminal pre-publish phase.
  for (int step = 0; step < 4; ++step) {
    auto scene = sample_scene(1);
    RenderJob job(std::move(scene));
    if (step >= 1) {
      expect(job.enter(RenderJobPhase::preparing).has_value(), "to preparing");
    }
    if (step >= 2) {
      expect(job.enter(RenderJobPhase::rendering).has_value(), "to rendering");
    }
    if (step >= 3) {
      expect(job.enter(RenderJobPhase::finishing).has_value(), "to finishing");
    }
    expect(job.cancel().has_value(), "cancel ok");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled");
  }
}

void check_post_publish_cancel_truthful() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  const auto target = dir / "published.mp4";
  auto scene = sample_scene(2);
  RenderJob job(std::move(scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.publish_mode = PublishMode::no_replace;
  request.durability = DurabilityClass::process_crash_visible;
  request.frame_count_limit = 2;

  auto result = export_scene_video(job, program, request,
                                   make_factory(AuditSinkOptions{}));
  expect(result.has_value(), "export ok");
  expect(result && result->published, "published");
  expect(std::filesystem::exists(target), "target exists");
  expect(is_published_phase(job.phase()), "published phase");

  auto cancelled = job.cancel();
  expect(!cancelled.has_value(), "late cancel rejected");
  expect(cancelled.error().code == "render_job.cancel_after_publish_gate",
         "cancel-after-publish code");
  expect(is_published_phase(job.phase()), "still published*");
  expect(std::filesystem::exists(target), "video still present");
  expect(job.first_error().has_value(), "reject latched");
  expect(job.first_error()->code == "render_job.cancel_after_publish_gate",
         "sticky reject code");
  std::filesystem::remove_all(dir);
}

void check_export_cancel_mid_render_and_late_callback() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  const auto target = dir / "cancelled.mp4";
  auto late = std::make_shared<LateCallbackContext>();
  auto scene = sample_scene(6);
  RenderJob job(std::move(scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.durability = DurabilityClass::process_crash_visible;
  request.frame_count_limit = 6;

  std::stop_source stop;
  request.on_progress = [&](std::uint64_t completed, std::uint64_t) {
    if (completed >= 1) {
      stop.request_stop();
    }
  };

  AuditSinkOptions options;
  options.late = late;
  auto result = export_scene_video(job, program, request, make_factory(options),
                                   stop.get_token());
  expect(!result.has_value(), "export cancelled");
  expect(result.error().kind == ErrorKind::cancelled, "cancelled kind");
  expect(job.phase() == RenderJobPhase::cancelled, "job cancelled");
  expect(!std::filesystem::exists(target), "no published target");

  {
    std::unique_lock lock(late->mutex);
    late->cv.wait_for(lock, std::chrono::seconds(2),
                      [&] { return late->late_finished; });
    expect(late->cancel_seen, "cancel observed by sink");
    expect(late->late_finished, "late callback finished");
  }
  expect(late->touched.load(), "late callback touched shared context");
  expect(!std::filesystem::exists(target), "still unpublished after late cb");
  std::filesystem::remove_all(dir);
}

void check_cancel_at_publish_gate_keeps_old_target() {
  const auto dir = make_temp_dir();
  expect(!dir.empty(), "temp dir");
  if (dir.empty()) {
    return;
  }
  const auto target = dir / "gate.mp4";
  {
    std::ofstream out(target, std::ios::binary);
    out << "keep-original";
  }
  auto scene = sample_scene(2);
  RenderJob job(std::move(scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = target;
  request.publish_mode = PublishMode::overwrite;
  request.durability = DurabilityClass::process_crash_visible;
  request.frame_count_limit = 2;

  std::stop_source stop;
  AuditSinkOptions options;
  options.request_stop_on_finish = &stop;
  auto result = export_scene_video(job, program, request, make_factory(options),
                                   stop.get_token());
  expect(!result.has_value(), "cancel at publish gate");
  expect(result.error().kind == ErrorKind::cancelled, "cancelled kind");
  expect(job.phase() == RenderJobPhase::cancelled, "cancelled phase");
  std::ifstream in(target, std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  expect(body == "keep-original", "pre-publish cancel keeps old target");
  std::filesystem::remove_all(dir);
}

void check_scheduler_abort_wakes_and_joins() {
  std::atomic_int started{0};
  std::atomic_int finished{0};
  FrameEvalScheduler::Config config;
  config.credit_capacity = 2;
  config.queue_capacity = 1;
  config.worker_count = 2;
  auto scheduler = FrameEvalScheduler::create(
      config,
      [&](std::uint64_t, std::stop_token stop)
          -> std::expected<void, instantee::diagnostics::Error> {
        started.fetch_add(1, std::memory_order_relaxed);
        while (!stop.stop_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        finished.fetch_add(1, std::memory_order_relaxed);
        return std::unexpected(instantee::diagnostics::Error{
            ExitCode::cancelled, ErrorKind::cancelled, "cancelled", "eval",
            "stopped"});
      });
  expect(scheduler.has_value(), "scheduler create");
  if (!scheduler) {
    return;
  }
  expect((*scheduler)->submit(0).has_value(), "submit 0");
  while (started.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  (*scheduler)->abort();
  auto take = (*scheduler)->take_next();
  expect(!take.has_value(), "take cancelled/closed");
  // Destroying the scheduler joins workers; late evaluate exits via stop.
  scheduler->reset();
  expect(finished.load() >= 1, "worker observed stop");
}

void check_queue_abort_and_stop_callback() {
  auto queue = BoundedQueue<int>::create(1);
  expect(queue.has_value(), "queue");
  if (!queue) {
    return;
  }
  expect(queue->push(1).has_value(), "seed");
  std::atomic_bool producer_done{false};
  std::atomic_bool saw_cancel{false};
  std::jthread producer([&] {
    auto result = queue->push(2);
    saw_cancel = !result && result.error().code == "cancelled";
    producer_done.store(true, std::memory_order_release);
  });
  while (queue->metrics().blocked_push_count == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::stop_source stop;
  std::atomic_int callback_hits{0};
  std::stop_callback cb(stop.get_token(), [&] noexcept {
    callback_hits.fetch_add(1, std::memory_order_relaxed);
  });
  stop.request_stop();
  queue->abort();
  producer.join();
  expect(saw_cancel.load(), "blocked push cancelled");
  expect(producer_done.load(), "producer joined");
  expect(callback_hits.load() == 1, "stop callback once");
  // Callback after abort: still safe; queue remains alive.
  expect(queue->size() == 0, "abort drained");
}

} // namespace

int main() {
  check_stop_every_pre_publish_phase();
  check_post_publish_cancel_truthful();
  check_export_cancel_mid_render_and_late_callback();
  check_cancel_at_publish_gate_keeps_old_target();
  check_scheduler_abort_wakes_and_joins();
  check_queue_abort_and_stop_callback();

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
