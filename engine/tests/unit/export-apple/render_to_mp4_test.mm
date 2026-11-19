#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/media_conformance.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::render_metal::Device;
using instantee::render_metal::TestPatternRenderer;
using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::MediaConformanceExpect;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::validate_mp4_conformance;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;

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

    for (FrameIndex frame{0}; frame < FrameIndex{kFrameCount}; ++frame) {
      // Spike patterns only define frames 0 and 1; alternate for motion.
      const auto pattern_index = static_cast<std::uint32_t>(frame.value() & 1U);
      auto rgba = renderer->render(pattern_index);
      if (!rgba || !rgba->valid_dimensions()) {
        return 6;
      }

      std::vector<std::uint8_t> bgra(rgba->rgba.size());
      if (!flatten_straight_rgba8_to_opaque_bgra8(rgba->rgba, bgra)) {
        return 7;
      }
      auto buffer = pool->acquire();
      if (!buffer || !buffer->fill_bgra8(bgra)) {
        return 8;
      }
      if (!writer->append(*buffer, frame)) {
        return 9;
      }
    }

    if (!writer->finish() || !writer->finished()) {
      return 10;
    }

    if (!validate_mp4_conformance(output,
                                  MediaConformanceExpect::media_v1_portrait(
                                      FrameIndex{kFrameCount}))) {
      return 11;
    }

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    return 0;
  }
}
