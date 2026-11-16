#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <unistd.h>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameClock;
using instantee::spike::apple::extract_presentation_timeline;
using instantee::spike::apple::inspect_media;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::validate_presentation_timeline;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;
using instantee::spike::metal::Device;
using instantee::spike::metal::TestPatternRenderer;

constexpr std::uint64_t kFrameCount = 30; // 1 s at 30/1
constexpr std::uint32_t kPoolCapacity = 32;

[[nodiscard]] std::filesystem::path make_temp_mp4() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-render-mp4-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / "render_spike.mp4";
}

} // namespace

int main() {
  @autoreleasepool {
    const auto output = make_temp_mp4();
    if (output.empty()) {
      return 1;
    }
    const auto directory = output.parent_path();

    auto device = Device::open();
    if (!device) {
      return 2;
    }
    auto renderer = TestPatternRenderer::create(*device);
    if (!renderer) {
      return 3;
    }

    // Encoder may retain every appended buffer until finish.
    auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(),
                                        kPoolCapacity);
    if (!pool) {
      return 4;
    }

    VideoWriterConfig config;
    config.output_path = output;
    auto writer = VideoWriter::create(config);
    if (!writer) {
      return 5;
    }

    for (std::uint64_t frame = 0; frame < kFrameCount; ++frame) {
      // Spike patterns only define frames 0 and 1; alternate for motion.
      const auto pattern_index = static_cast<std::uint32_t>(frame & 1U);
      auto rgba = renderer->render(pattern_index);
      if (!rgba || !rgba->valid_dimensions()) {
        return 6;
      }

      auto buffer = pool->acquire();
      if (!buffer || !buffer->fill_rgba8(rgba->rgba)) {
        return 7;
      }
      if (!writer->append(*buffer, frame)) {
        return 8;
      }
    }

    if (!writer->finish() || !writer->finished()) {
      return 9;
    }

    const auto info = inspect_media(output);
    if (!info) {
      return 10;
    }
    if (!info->playable || !info->h264 || info->video_track_count != 1 ||
        info->audio_track_count != 0 || info->width != 1080 ||
        info->height != 1920) {
      return 11;
    }
    if (std::abs(info->nominal_frame_rate - 30.0) > 0.05) {
      return 12;
    }

    const auto timeline = extract_presentation_timeline(output);
    if (!timeline || !validate_presentation_timeline(
                         *timeline, FrameClock::media_30_1(), kFrameCount)) {
      return 13;
    }

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    return 0;
  }
}
