#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>

namespace instantee::spike::apple {

// Expected V1 media profile for an independently inspected MP4. Zero
// `frame_count` rejects the file (empty exports are never conforming).
struct MediaConformanceExpect {
  std::uint32_t width = 1080;
  std::uint32_t height = 1920;
  core::FrameIndex frame_count{0};
  core::FrameClock clock = core::FrameClock::media_30_1();
  double nominal_frame_rate = 30.0;
  double nominal_frame_rate_tolerance = 0.05;
  bool require_h264 = true;
  bool require_rec709 = true;
  bool require_single_video_track = true;
  bool require_no_audio = true;
  bool require_playable = true;
  // Full decode path for exact PTS / frame-count checks (CPP contracts §4).
  bool decode_timeline = true;

  [[nodiscard]] static MediaConformanceExpect
  media_v1_portrait(core::FrameIndex frames) noexcept {
    MediaConformanceExpect expect;
    expect.frame_count = frames;
    return expect;
  }
};

struct MediaConformanceReport {
  MediaInspection inspection{};
  PresentationTimeline timeline{};
};

// Fail-closed pre-publish / suite validator: metadata, Rec.709 tags, silence,
// playability, and (when enabled) exact FrameClock PTS after independent
// decode.
[[nodiscard]] std::expected<MediaConformanceReport, diagnostics::Error>
validate_mp4_conformance(const std::filesystem::path &path,
                         const MediaConformanceExpect &expect);

} // namespace instantee::spike::apple
