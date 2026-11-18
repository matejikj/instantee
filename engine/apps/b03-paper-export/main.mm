#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/rec709_handoff.hpp>
#include <instantee/export_core/write_all.hpp>
#include <instantee/runtime/frame_preview.hpp>
#include <instantee/runtime/frame_samples.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/runtime/stop_signals.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/lockfile.hpp>
#include <instantee/telemetry_apple/process_sampler.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using clock_type = std::chrono::steady_clock;

void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("B-03 paper export failed");
  }
}

std::uint64_t now_ns(clock_type::time_point epoch) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() -
                                                           epoch)
          .count());
}

class FileDescriptor {
public:
  explicit FileDescriptor(int value) noexcept : value_(value) {}
  ~FileDescriptor() {
    if (value_ >= 0) {
      ::close(value_);
    }
  }
  FileDescriptor(const FileDescriptor &) = delete;
  FileDescriptor &operator=(const FileDescriptor &) = delete;
  int get() const noexcept { return value_; }
  int close() noexcept { return ::close(std::exchange(value_, -1)); }

private:
  int value_;
};

void write_json(const std::filesystem::path &path, const std::string &text) {
  auto tx = instantee::export_core::OutputTransaction::begin(
      {path, instantee::export_core::PublishMode::no_replace,
       instantee::export_core::DurabilityClass::process_crash_visible});
  require(tx.has_value());
  FileDescriptor fd{::open(tx->staging_file().c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  require(fd.get() >= 0);
  auto result = instantee::export_core::write_all(
      fd.get(), std::as_bytes(std::span{text.data(), text.size()}));
  const int closed = fd.close();
  require(result.has_value() && closed == 0);
  auto published = tx->commit();
  require(published && published->published && !published->post_publish_error);
}

void optional_number(std::ostream &out, std::optional<std::uint64_t> number) {
  if (number) {
    out << *number;
  } else {
    out << "null";
  }
}

struct ExportCounters {
  std::uint64_t scheduled = 0;
  std::uint64_t admitted = 0;
  std::uint64_t accepted = 0;
  std::uint64_t verified = 0;
  bool published = false;
  bool started = false;
};

int run(const std::filesystem::path &directory, std::uint32_t frames,
        bool telemetry, std::size_t resource_capacity, std::stop_source &stop,
        ExportCounters &counters) {
  counters.started = true;
  const auto epoch = clock_type::now();
  const std::filesystem::path root{INSTANTEE_SOURCE_DIR};
  const auto scene_path =
      root / "contracts/fixtures/b03-paper/b03-historical-reel.scene.json";
  const auto lock_path = root / "assets/paper-cinema/paper-cinema.lock.json";

  // Offline lock verification before export (traceability gate).
  auto lock = instantee::scene_ir::load_asset_lockfile_file(lock_path);
  require(lock.has_value());
  require(instantee::scene_ir::verify_asset_lockfile(*lock, root).has_value());

  instantee::scene_ir::LoadOptions load_options;
  load_options.project_root = root;
  load_options.enforce_uri_sandbox = true;
  auto document =
      instantee::scene_ir::load_scene_file(scene_path, load_options);
  require(document.has_value());
  require(instantee::scene_ir::validate_graph(*document).has_value());
  auto compiled = instantee::compiler::compile_runtime_scene(*document);
  require(compiled.has_value() && *compiled != nullptr);
  require((*compiled)->duration_frames().value() == 1800);
  require((*compiled)->canvas_width() == 1080 &&
          (*compiled)->canvas_height() == 1920);
  require(frames > 0 && frames <= (*compiled)->duration_frames().value());

  std::unique_ptr<instantee::telemetry_apple::ProcessSampler> sampler;
  std::optional<instantee::runtime::FrameSamples> timings;
  if (telemetry) {
    auto started = instantee::telemetry_apple::ProcessSampler::start(
        epoch, resource_capacity);
    auto log = instantee::runtime::FrameSamples::create(frames);
    require(started.has_value() && log.has_value());
    sampler = std::move(*started);
    timings = std::move(*log);
  }

  auto tx = instantee::export_core::OutputTransaction::begin(
      {directory / "output.mp4",
       instantee::export_core::PublishMode::no_replace,
       instantee::export_core::DurabilityClass::process_crash_visible});
  require(tx.has_value());

  // Locked measurement profile uses E=8 encoder pool capacity. Prove the
  // allocation threshold before the sequential Scene IR → preview export,
  // then size the live sink pool to the same contract.
  constexpr std::uint32_t kEncoderPoolE = 8;
  std::uint64_t pool_peak = 0;
  {
    auto pool = instantee::spike::apple::PixelBufferPool::create(
        instantee::spike::apple::PixelBufferFormat::media_1080x1920(),
        kEncoderPoolE);
    require(pool.has_value());
    std::vector<instantee::spike::apple::PixelBuffer> prewarm;
    prewarm.reserve(kEncoderPoolE);
    for (std::uint32_t i = 0; i < kEncoderPoolE; ++i) {
      auto buffer = pool->acquire(stop.get_token());
      require(buffer.has_value());
      prewarm.push_back(std::move(*buffer));
    }
    pool_peak = prewarm.size();
  }
  require(pool_peak == kEncoderPoolE);

  auto sink = instantee::export_apple::AppleVideoSink::create(
      tx->staging_file(), kEncoderPoolE);
  require(sink.has_value());

  instantee::runtime::RenderJob job(std::move(*compiled));
  require(
      job.enter(instantee::runtime::RenderJobPhase::preparing, stop.get_token())
          .has_value());
  const auto &scene = job.scene();
  instantee::export_core::VideoSettings settings{
      .width = scene.canvas_width(),
      .height = scene.canvas_height(),
      .frame_rate = scene.frame_rate()};
  require((*sink)->begin(settings).has_value());
  instantee::export_core::CpuFrameBridge bridge(std::move(*sink));
  require(
      job.enter(instantee::runtime::RenderJobPhase::rendering, stop.get_token())
          .has_value());

  auto clock = instantee::core::FrameClock::create(scene.frame_rate());
  require(clock.has_value());
  // Scene IR tracks must drive preview evaluation; an empty program leaves
  // authored opacity at 0 and measures an empty-canvas lie.
  auto program =
      instantee::compiler::compile_animation_program(*document, scene);
  require(program.has_value());
  instantee::preview::FramePngRequest raster{};
  const std::uint64_t slot_bytes =
      static_cast<std::uint64_t>(scene.canvas_width()) *
      static_cast<std::uint64_t>(scene.canvas_height()) * 4ULL;
  std::uint64_t in_flight_peak = 0;

  for (std::uint32_t index = 0; index < frames; ++index) {
    if (stop.stop_requested()) {
      throw std::runtime_error("cancelled");
    }
    instantee::runtime::FrameSample sample{};
    sample.frame = index;
    if (telemetry) {
      sample.request_ns = now_ns(epoch);
    }
    ++counters.scheduled;
    require(job.acquire_frame_slot(slot_bytes).has_value());
    in_flight_peak = std::max(in_flight_peak, job.in_flight_frames());
    ++counters.admitted;
    if (telemetry) {
      sample.admitted_ns = now_ns(epoch);
      sample.eval_start_ns = now_ns(epoch);
    }
    const instantee::core::FrameIndex at{index};
    auto preview = instantee::runtime::preview_frame_at(scene, *program, at);
    require(preview.has_value());
    if (telemetry) {
      sample.eval_done_ns = now_ns(epoch);
      sample.gpu_submit_ns = sample.eval_done_ns;
    }
    auto image = instantee::preview::rasterize_preview_frame(
        scene, preview->state, raster);
    require(image.has_value());
    if (telemetry) {
      sample.gpu_completed_observed_ns = now_ns(epoch);
      sample.sink_attempt_ns = now_ns(epoch);
    }
    auto pts = clock->pts(at);
    require(pts.has_value());
    auto published = instantee::export_core::publish_rec709_rgba8(
        bridge, image->width_px, image->height_px, image->rgba, at, *pts,
        stop.get_token());
    require(published.has_value());
    if (telemetry) {
      sample.sink_accepted_ns = now_ns(epoch);
      require(timings->record(sample));
    }
    require(job.note_frame_appended().has_value());
    job.release_frame_slot(slot_bytes);
    ++counters.accepted;
  }

  require(
      job.enter(instantee::runtime::RenderJobPhase::finishing, stop.get_token())
          .has_value());
  const auto finalize_start = clock_type::now();
  require(bridge.sink().finish().has_value());

  auto info = instantee::spike::apple::inspect_media(tx->staging_file());
  require(info.has_value() && info->h264 && info->width == 1080 &&
          info->height == 1920 && info->video_track_count == 1 &&
          info->audio_track_count == 0 && info->playable);
  auto timeline = instantee::spike::apple::extract_presentation_timeline(
      tx->staging_file());
  require(timeline.has_value());
  require(instantee::spike::apple::validate_presentation_timeline(
              *timeline, *clock, instantee::core::FrameIndex{frames})
              .has_value());
  counters.verified = frames;

  require(job.enter(instantee::runtime::RenderJobPhase::publishing,
                    stop.get_token())
              .has_value());
  auto publication = tx->commit(stop.get_token());
  require(publication && publication->published &&
          !publication->post_publish_error);
  counters.published = true;
  require(job.note_publish_outcome(true, true, false).has_value());

  const double finalize_ms = std::chrono::duration<double, std::milli>(
                                 clock_type::now() - finalize_start)
                                 .count();
  if (sampler) {
    sampler->finish();
  }

  std::ostringstream latency;
  latency.imbue(std::locale::classic());
  latency << '[';
  if (timings) {
    bool comma = false;
    for (auto s : timings->samples()) {
      if (comma) {
        latency << ',';
      }
      comma = true;
      latency << '[' << s.frame << ',' << s.request_ns << ',' << s.admitted_ns
              << ',' << s.eval_start_ns << ',' << s.eval_done_ns << ','
              << s.gpu_submit_ns << ',' << s.gpu_completed_observed_ns << ','
              << s.sink_attempt_ns << ',' << s.sink_accepted_ns << ']';
    }
  }
  latency << ']';
  require(latency.good());
  write_json(directory / "frames.json", latency.str());

  std::ostringstream resources;
  resources.imbue(std::locale::classic());
  resources << '[';
  if (sampler) {
    bool comma = false;
    for (auto s : sampler->samples()) {
      if (comma) {
        resources << ',';
      }
      comma = true;
      resources << '[' << s.observed_ns << ',';
      optional_number(resources, s.user_ns);
      resources << ',';
      optional_number(resources, s.system_ns);
      resources << ',';
      optional_number(resources, s.rss_current_bytes);
      resources << ',';
      optional_number(resources, s.rss_peak_bytes);
      resources << ',';
      optional_number(resources, s.physical_footprint_bytes);
      resources << ']';
    }
  }
  resources << ']';
  require(resources.good());
  write_json(directory / "resources.json", resources.str());

  auto summary = timings ? timings->summary() : std::nullopt;
  std::ostringstream report;
  report.imbue(std::locale::classic());
  report << "{\"telemetry_enabled\":" << (telemetry ? "true" : "false")
         << ",\"validation_mode\":\"" << INSTANTEE_METRICS_VALIDATION
         << "\",\"scheduled\":" << counters.scheduled
         << ",\"admitted\":" << counters.admitted
         << ",\"accepted\":" << counters.accepted
         << ",\"verified\":" << counters.verified
         << ",\"published\":true,\"finalize_ms\":" << finalize_ms
         << ",\"in_flight_peak\":" << in_flight_peak
         << ",\"queue_peak\":1,\"encoder_pool_peak\":" << pool_peak
         << ",\"frame_lost_samples\":"
         << (timings ? timings->lost_samples() : 0)
         << ",\"resource_lost_samples\":"
         << (sampler ? sampler->lost_samples() : 0) << ",\"latency_summary\":";
  if (summary) {
    report << '[' << summary->sample_count << ',' << summary->min << ','
           << summary->p50 << ',' << summary->p95 << ',' << summary->p99 << ','
           << summary->max << ']';
  } else {
    report << "null";
  }
  report << '}';
  require(report.good());
  write_json(directory / "child.json", report.str());
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  ExportCounters counters;
  try {
    require(argc == 4 || argc == 5);
    const std::string_view number{argv[2]}, mode{argv[3]};
    std::uint32_t frames = 0;
    const auto parsed =
        std::from_chars(number.data(), number.data() + number.size(), frames);
    require(parsed.ec == std::errc{} &&
            parsed.ptr == number.data() + number.size() && frames > 0 &&
            frames <= 9000);
    require(mode == "on" || mode == "off");
    std::size_t capacity = 65536;
    if (argc == 5) {
      const std::string_view input{argv[4]};
      const auto result =
          std::from_chars(input.data(), input.data() + input.size(), capacity);
      require(result.ec == std::errc{} &&
              result.ptr == input.data() + input.size() && capacity > 0 &&
              capacity <= 65536);
    }
    std::stop_source stop;
    auto signals = instantee::runtime::StopSignals::start(stop);
    require(signals.has_value());
    @autoreleasepool {
      return run(argv[1], frames, mode == "on", capacity, stop, counters);
    }
  } catch (...) {
    if (counters.started) {
      try {
        std::ostringstream partial;
        partial << "{\"partial\":true,\"scheduled\":" << counters.scheduled
                << ",\"admitted\":" << counters.admitted
                << ",\"accepted\":" << counters.accepted
                << ",\"verified\":" << counters.verified
                << ",\"published\":" << (counters.published ? "true" : "false")
                << ",\"validation_mode\":\"" << INSTANTEE_METRICS_VALIDATION
                << "\"}";
        write_json(std::filesystem::path{argv[1]} / "child.json",
                   partial.str());
      } catch (...) {
      }
    }
    std::fprintf(stderr,
                 "{\"code\":\"b03_paper_export_failed\",\"phase\":"
                 "\"measurement\",\"published\":%s,\"durability\":\"%s\"}\n",
                 counters.published ? "true" : "false",
                 counters.published ? "unknown" : "not_published");
    return 6;
  }
}
