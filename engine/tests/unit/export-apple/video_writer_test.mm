#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::spike::apple::inspect_media;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;
using instantee::spike::apple::detail::PixelBufferAccess;

[[nodiscard]] std::filesystem::path make_temp_mp4() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-video-writer-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / "spike.mp4";
}

[[nodiscard]] int fill_solid(instantee::spike::apple::PixelBuffer &buffer,
                             std::uint8_t value) {
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(buffer.format().width) *
          static_cast<std::size_t>(buffer.format().height) * 4U,
      value);
  return buffer.fill_bgra8(pixels) ? 0 : 1;
}

[[nodiscard]] int check_stop_wakes_readiness(VideoWriter &writer) {
  std::stop_source stopped;
  stopped.request_stop();
  const auto prestopped = writer.wait_until_ready(stopped.get_token());
  if (prestopped || prestopped.error().code != "cancelled") {
    return 10;
  }

  std::atomic_bool started{false};
  std::optional<std::string_view> wait_code;
  std::atomic_bool wait_ok{false};
  std::jthread waiter{[&](std::stop_token stop) {
    started.store(true, std::memory_order_release);
    const auto result = writer.wait_until_ready(stop);
    if (result) {
      wait_ok.store(true, std::memory_order_release);
    } else {
      wait_code = result.error().code;
    }
  }};

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  writer.cancel();
  waiter.request_stop();
  waiter.join();
  if (!writer.cancelled()) {
    return 11;
  }
  if (!(wait_ok.load(std::memory_order_acquire) ||
        (wait_code.has_value() && *wait_code == "cancelled"))) {
    return 12;
  }

  const auto after_cancel = writer.wait_until_ready();
  if (after_cancel || after_cancel.error().code != "cancelled") {
    return 13;
  }
  return 0;
}

[[nodiscard]] int check_video_only_export() {
  const auto output = make_temp_mp4();
  if (output.empty()) {
    return 20;
  }
  const auto directory = output.parent_path();

  // Encoder retains appended buffers until finish; capacity must cover
  // in-flight frames or acquire correctly waits for recycle.
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 4);
  if (!pool) {
    return 21;
  }

  VideoWriterConfig config;
  config.output_path = output;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 22;
  }

  for (FrameIndex frame{0}; frame < FrameIndex{3}; ++frame) {
    auto buffer = pool->acquire();
    if (!buffer || fill_solid(*buffer, static_cast<std::uint8_t>(
                                           40U + frame.value())) != 0) {
      return 23;
    }
    if (!writer->append(*buffer, frame)) {
      return 24;
    }
    if (!PixelBufferAccess::empty(*buffer)) {
      return 25;
    }
  }

  if (!writer->finish() || !writer->finished()) {
    return 26;
  }

  const auto info = inspect_media(output);
  if (!info || !info->playable || !info->h264 || !info->rec709_primaries ||
      !info->rec709_transfer || !info->rec709_ycbcr_matrix ||
      info->video_track_count != 1 || info->audio_track_count != 0 ||
      info->width != 1080 || info->height != 1920 ||
      std::abs(info->nominal_frame_rate - 30.0) > 0.05) {
    return 27;
  }

  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    const auto output = make_temp_mp4();
    if (output.empty()) {
      return 1;
    }
    const auto directory = output.parent_path();

    VideoWriterConfig config;
    config.output_path = output;
    auto writer = VideoWriter::create(config);
    if (!writer) {
      return 2;
    }
    if (const int code = check_stop_wakes_readiness(*writer); code != 0) {
      return code;
    }

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    return check_video_only_export();
  }
}
