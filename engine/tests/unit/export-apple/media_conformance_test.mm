#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/media_conformance.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameIndex;
using instantee::spike::apple::MediaConformanceExpect;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::validate_mp4_conformance;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-conform-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] int export_solid_reference(const std::filesystem::path &output,
                                         FrameIndex frame_count,
                                         std::uint8_t fill) {
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 8);
  if (!pool) {
    return 1;
  }
  VideoWriterConfig config;
  config.output_path = output;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 2;
  }
  const auto &format = PixelBufferFormat::media_1080x1920();
  std::vector<std::uint8_t> solid(static_cast<std::size_t>(format.width) *
                                      static_cast<std::size_t>(format.height) *
                                      4U,
                                  fill);
  for (FrameIndex frame{0}; frame < frame_count; ++frame) {
    auto buffer = pool->acquire();
    if (!buffer || !buffer->fill_bgra8(solid)) {
      return 3;
    }
    if (!writer->append(*buffer, frame)) {
      return 4;
    }
  }
  if (!writer->finish() || !writer->finished()) {
    return 5;
  }
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    const auto directory = make_temp_dir();
    if (directory.empty()) {
      return EXIT_FAILURE;
    }

    // Reference set: short V1-profile clips that every release path must
    // accept.
    constexpr FrameIndex kRefA{15}; // 0.5 s
    constexpr FrameIndex kRefB{30}; // 1.0 s
    const auto ref_a = directory / "reference_a.mp4";
    const auto ref_b = directory / "reference_b.mp4";
    expect(export_solid_reference(ref_a, kRefA, 0x30) == 0, "export ref A");
    expect(export_solid_reference(ref_b, kRefB, 0x50) == 0, "export ref B");

    {
      auto ok = validate_mp4_conformance(
          ref_a, MediaConformanceExpect::media_v1_portrait(kRefA));
      expect(ok.has_value(), "ref A conforms");
      if (ok) {
        expect(ok->inspection.audio_track_count == 0, "ref A silent");
        expect(ok->timeline.presentation_timestamps.size() == kRefA.value(),
               "ref A frame count");
      }
    }
    {
      auto ok = validate_mp4_conformance(
          ref_b, MediaConformanceExpect::media_v1_portrait(kRefB));
      expect(ok.has_value(), "ref B conforms");
    }

    // Invalid / mismatched metadata must fail closed with stable codes.
    {
      auto missing = validate_mp4_conformance(
          directory / "missing.mp4",
          MediaConformanceExpect::media_v1_portrait(kRefA));
      expect(!missing.has_value(), "missing file rejected");
    }
    {
      const auto garbage = directory / "garbage.mp4";
      {
        std::ofstream out(garbage, std::ios::binary);
        out << "not-an-mp4-container";
      }
      auto bad = validate_mp4_conformance(
          garbage, MediaConformanceExpect::media_v1_portrait(kRefA));
      expect(!bad.has_value(), "garbage bytes rejected");
    }
    {
      // Truncate a valid MP4 so container metadata becomes unusable.
      const auto truncated = directory / "truncated.mp4";
      std::filesystem::copy_file(ref_a, truncated);
      {
        std::ofstream out(truncated,
                          std::ios::binary | std::ios::in | std::ios::out);
        out.seekp(0);
        const char poison[] = {'\0', '\0', '\0', '\0'};
        out.write(poison, sizeof(poison));
      }
      auto bad = validate_mp4_conformance(
          truncated, MediaConformanceExpect::media_v1_portrait(kRefA));
      expect(!bad.has_value(), "truncated container rejected");
    }
    {
      auto wrong_count = validate_mp4_conformance(
          ref_a, MediaConformanceExpect::media_v1_portrait(FrameIndex{14}));
      expect(!wrong_count.has_value(), "frame count mismatch rejected");
      expect(!wrong_count.has_value() &&
                 wrong_count.error().code == "frame_count_mismatch",
             "frame_count_mismatch code");
    }
    {
      auto wrong_size = MediaConformanceExpect::media_v1_portrait(kRefA);
      wrong_size.width = 720;
      auto bad = validate_mp4_conformance(ref_a, wrong_size);
      expect(!bad.has_value() && bad.error().code == "conformance.width",
             "width mismatch code");
    }
    {
      auto empty_expect = MediaConformanceExpect::media_v1_portrait(kRefA);
      empty_expect.frame_count = FrameIndex{0};
      auto bad = validate_mp4_conformance(ref_a, empty_expect);
      expect(!bad.has_value() && bad.error().code == "conformance.empty_expect",
             "empty expect rejected");
    }

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    if (failures != 0) {
      std::cerr << failures << " assertion(s) failed\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
}
