#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/export_core/write_all.hpp>
#include <instantee/render_metal/test_pattern.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/frame_samples.hpp>
#include <instantee/runtime/stop_signals.hpp>
#include <instantee/telemetry_apple/process_sampler.hpp>

#include <atomic>
#include <charconv>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {
using namespace instantee;
using clock_type = std::chrono::steady_clock;

void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("Measurement export failed");
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
  auto tx = export_core::OutputTransaction::begin(
      {path, export_core::PublishMode::no_replace,
       export_core::DurabilityClass::process_crash_visible});
  require(tx.has_value());
  FileDescriptor fd{::open(tx->staging_file().c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
  require(fd.get() >= 0);
  auto result = export_core::write_all(
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

struct Frame {
  runtime::FrameCredit credit;
  spike::metal::Rgba8Frame pixels;
  runtime::FrameSample timing;
};
using Queue = runtime::BoundedQueue<Frame>;
struct Shutdown {
  std::stop_source &stop;
  runtime::FrameCreditPool &credits;
  Queue &queue;
  std::jthread &producer;
  ~Shutdown() {
    if (!producer.joinable()) {
      return;
    }
    stop.request_stop();
    credits.close();
    queue.abort();
    producer.join();
  }
};

struct ExportCounters {
  std::uint64_t scheduled = 0, admitted = 0, accepted = 0, verified = 0;
  bool published = false;
  bool started = false;
};

int run(const std::filesystem::path &directory, std::uint32_t frames,
        bool telemetry, std::size_t resource_capacity, std::stop_source &stop,
        ExportCounters &counters) {
  counters.started = true;
  const auto epoch = clock_type::now();
  std::unique_ptr<telemetry_apple::ProcessSampler> sampler;
  std::optional<runtime::FrameSamples> timings;
  if (telemetry) {
    auto started =
        telemetry_apple::ProcessSampler::start(epoch, resource_capacity);
    auto log = runtime::FrameSamples::create(frames);
    require(started.has_value() && log.has_value());
    sampler = std::move(*started);
    timings = std::move(*log);
  }
  auto tx = export_core::OutputTransaction::begin(
      {directory / "output.mp4", export_core::PublishMode::no_replace,
       export_core::DurabilityClass::process_crash_visible});
  auto device = spike::metal::Device::open();
  require(tx.has_value() && device.has_value());
  auto renderer = spike::metal::TestPatternRenderer::create(*device);
  auto pool = spike::apple::PixelBufferPool::create(
      spike::apple::PixelBufferFormat::media_1080x1920(), 8);
  require(renderer.has_value() && pool.has_value());
  // Reach the allocation threshold during preparation. This measures the exact
  // pool allocation peak (including prewarm) without inferring encoder retains.
  std::uint64_t pool_peak = 0;
  {
    std::vector<spike::apple::PixelBuffer> prewarm;
    prewarm.reserve(8);
    for (unsigned i = 0; i < 8; ++i) {
      auto buffer = pool->acquire(stop.get_token());
      require(buffer.has_value());
      prewarm.push_back(std::move(*buffer));
    }
    pool_peak = prewarm.size();
  }
  auto writer = spike::apple::VideoWriter::create({tx->staging_file()});
  auto credits = runtime::FrameCreditPool::create(2);
  auto queue = Queue::create(1);
  require(writer.has_value() && credits.has_value() && queue.has_value());
  std::atomic_bool producer_failed{false};
  auto &accepted = counters.accepted;
  auto &scheduled = counters.scheduled;
  auto &admitted = counters.admitted;
  std::vector<std::uint8_t> bgra(1080U * 1920U * 4U);
  {
    std::jthread producer([&] {
      try {
        for (std::uint32_t index = 0; index < frames; ++index) {
          @autoreleasepool {
            if (stop.stop_requested()) {
              break;
            }
            runtime::FrameSample sample{};
            sample.frame = index;
            if (telemetry) {
              sample.request_ns = now_ns(epoch);
            }
            ++scheduled;
            auto credit = credits->acquire(stop.get_token());
            require(credit.has_value());
            ++admitted;
            if (stop.stop_requested()) {
              break;
            }
            if (telemetry) {
              sample.admitted_ns = now_ns(epoch);
              sample.eval_start_ns = sample.eval_done_ns =
                  sample.gpu_submit_ns = now_ns(epoch);
            }
            auto lease = renderer->submit(index & 1U);
            require(lease.has_value());
            auto pixels = lease->take_pixels();
            require(pixels.has_value());
            if (telemetry) {
              sample.gpu_completed_observed_ns = now_ns(epoch);
            }
            Frame frame{std::move(*credit), std::move(*pixels), sample};
            require(
                queue->push(std::move(frame), stop.get_token()).has_value());
          }
        }
        queue->close();
      } catch (...) {
        producer_failed = true;
        stop.request_stop();
        credits->close();
        queue->abort();
      }
    });
    Shutdown shutdown{stop, *credits, *queue, producer};
    while (accepted < frames) {
      @autoreleasepool {
        auto frame = queue->pop(stop.get_token());
        require(frame.has_value() && !stop.stop_requested());
        require(frame->timing.frame == accepted);
        if (telemetry) {
          frame->timing.sink_attempt_ns = now_ns(epoch);
        }
        require(spike::apple::flatten_straight_rgba8_to_opaque_bgra8(
                    frame->pixels.rgba, bgra)
                    .has_value());
        auto buffer = pool->acquire(stop.get_token());
        require(buffer.has_value() && buffer->fill_bgra8(bgra).has_value());
        require(
            writer
                ->append(*buffer, core::FrameIndex{accepted}, stop.get_token())
                .has_value());
        if (telemetry) {
          frame->timing.sink_accepted_ns = now_ns(epoch);
          static_cast<void>(timings->record(frame->timing));
        }
        ++accepted;
      }
    }
    producer.join();
    require(!producer_failed && !stop.stop_requested());
  }
  require(scheduled == frames && admitted == frames && accepted == frames);
  const auto finalize_start = clock_type::now();
  require(writer->finish(stop.get_token()).has_value());
  auto timeline =
      spike::apple::extract_presentation_timeline(tx->staging_file());
  auto info = spike::apple::inspect_media(tx->staging_file());
  require(timeline.has_value() && info && info->h264 && info->width == 1080 &&
          info->height == 1920 && info->video_track_count == 1 &&
          info->audio_track_count == 0);
  require(
      spike::apple::validate_presentation_timeline(
          *timeline, core::FrameClock::media_30_1(), core::FrameIndex{frames})
          .has_value());
  counters.verified = frames;
  auto publication = tx->commit(stop.get_token());
  counters.published = publication && publication->published;
  require(publication && publication->published &&
          !publication->post_publish_error);
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
         << "\",\"scheduled\":" << scheduled << ",\"admitted\":" << admitted
         << ",\"accepted\":" << accepted << ",\"verified\":" << frames
         << ",\"published\":true,\"finalize_ms\":" << finalize_ms
         << ",\"in_flight_peak\":" << credits->metrics().high_water
         << ",\"queue_peak\":" << queue->metrics().high_water
         << ",\"encoder_pool_peak\":" << pool_peak << ",\"frame_lost_samples\":"
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
    auto signals = runtime::StopSignals::start(stop);
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
                 "{\"code\":\"measurement_export_failed\",\"phase\":"
                 "\"measurement\",\"published\":%s,\"durability\":\"%s\"}\n",
                 counters.published ? "true" : "false",
                 counters.published ? "unknown" : "not_published");
    return 6;
  }
}
