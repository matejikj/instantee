#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::core::FrameClock;
using instantee::core::FrameIndex;
using instantee::core::RationalTime;
using instantee::spike::apple::extract_presentation_timeline;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::PresentationTimeline;
using instantee::spike::apple::validate_presentation_timeline;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;

constexpr std::uint32_t kPoolCapacity = 8;
constexpr FrameIndex kFrames60s{1800};  // 60 s at 30/1
constexpr FrameIndex kFrames300s{9000}; // 300 s at 30/1

[[nodiscard]] std::filesystem::path make_temp_mp4(const char *name) {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-pts-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / name;
}

// Timing-only export: pixel contents do not affect presentation timestamps.
[[nodiscard]] int export_timeline(FrameIndex frame_count,
                                  const std::filesystem::path &output) {
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(),
                                      kPoolCapacity);
  if (!pool) {
    return 1;
  }

  VideoWriterConfig config;
  config.output_path = output;
  auto writer = VideoWriter::create(config);
  if (!writer) {
    return 2;
  }

  // Solid fill keeps the encoder fed with valid 32BGRA frames; timing is what
  // this test asserts, not color fidelity.
  std::vector<std::uint8_t> solid(
      static_cast<std::size_t>(PixelBufferFormat::media_1080x1920().width) *
          static_cast<std::size_t>(
              PixelBufferFormat::media_1080x1920().height) *
          4U,
      0x20);
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

[[nodiscard]] int check_exported_timeline(FrameIndex frame_count,
                                          const char *file_name) {
  const auto output = make_temp_mp4(file_name);
  if (output.empty()) {
    return 10;
  }
  const auto directory = output.parent_path();

  if (const int code = export_timeline(frame_count, output); code != 0) {
    return 10 + code;
  }

  const auto timeline = extract_presentation_timeline(output);
  if (!timeline) {
    return 20;
  }

  const auto clock = FrameClock::media_30_1();
  if (!validate_presentation_timeline(*timeline, clock, frame_count)) {
    return 21;
  }

  if (timeline->presentation_timestamps.size() != frame_count.value() ||
      timeline->presentation_timestamps.front() != *RationalTime::make(0, 1) ||
      timeline->asset_duration !=
          *RationalTime::make(frame_count.value(), 30)) {
    return 22;
  }

  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  return 0;
}

[[nodiscard]] int check_validator_rejects_faults() {
  const auto clock = FrameClock::media_30_1();
  PresentationTimeline good{};
  good.presentation_timestamps = {*RationalTime::make(0, 30),
                                  *RationalTime::make(1, 30),
                                  *RationalTime::make(2, 30)};
  good.asset_duration = *RationalTime::make(3, 30);
  if (!validate_presentation_timeline(good, clock, FrameIndex{3})) {
    return 30;
  }

  PresentationTimeline wrong_count = good;
  wrong_count.presentation_timestamps.pop_back();
  if (validate_presentation_timeline(wrong_count, clock, FrameIndex{3}) ||
      validate_presentation_timeline(wrong_count, clock, FrameIndex{3})
              .error()
              .code != "frame_count_mismatch") {
    return 31;
  }

  PresentationTimeline gap = good;
  gap.presentation_timestamps[1] = *RationalTime::make(2, 30);
  gap.presentation_timestamps[2] = *RationalTime::make(3, 30);
  // Keep declared duration correct so the PTS mismatch is the first failure.
  if (validate_presentation_timeline(gap, clock, FrameIndex{3}) ||
      validate_presentation_timeline(gap, clock, FrameIndex{3}).error().code !=
          "pts_mismatch") {
    return 32;
  }

  PresentationTimeline duplicate = good;
  duplicate.presentation_timestamps[2] = *RationalTime::make(1, 30);
  if (validate_presentation_timeline(duplicate, clock, FrameIndex{3}) ||
      validate_presentation_timeline(duplicate, clock, FrameIndex{3})
              .error()
              .code != "pts_mismatch") {
    return 33;
  }

  PresentationTimeline wrong_duration = good;
  wrong_duration.asset_duration = *RationalTime::make(2, 30);
  if (validate_presentation_timeline(wrong_duration, clock, FrameIndex{3}) ||
      validate_presentation_timeline(wrong_duration, clock, FrameIndex{3})
              .error()
              .code != "duration_mismatch") {
    return 34;
  }

  if (validate_presentation_timeline(good, clock, FrameIndex{0}) ||
      validate_presentation_timeline(good, clock, FrameIndex{0}).error().code !=
          "empty_timeline") {
    return 35;
  }
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    if (const int code = check_validator_rejects_faults(); code != 0) {
      return code;
    }
    if (const int code = check_exported_timeline(kFrames60s, "sixty.mp4");
        code != 0) {
      return code;
    }
    if (const int code = check_exported_timeline(kFrames300s, "five_min.mp4");
        code != 0) {
      return code;
    }
    return 0;
  }
}
