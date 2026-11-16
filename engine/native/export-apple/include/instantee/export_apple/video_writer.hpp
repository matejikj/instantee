#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <vector>

namespace instantee::spike::apple {

struct VideoWriterConfig {
  std::filesystem::path output_path;
  PixelBufferFormat format = PixelBufferFormat::media_1080x1920();
  core::FrameRate frame_rate = core::FrameRate::media_30_1();
};

// AVFoundation H.264 video-only writer behind a C++ PImpl. Public headers stay
// free of Objective-C/AVFoundation types. stop_token wakes readiness waits;
// native callbacks never propagate C++ or Objective-C exceptions.
class VideoWriter {
public:
  VideoWriter(VideoWriter &&) noexcept;
  VideoWriter &operator=(VideoWriter &&) noexcept;
  ~VideoWriter();

  VideoWriter(const VideoWriter &) = delete;
  VideoWriter &operator=(const VideoWriter &) = delete;

  // Creates a video-only AVAssetWriter session for a non-existent output path.
  [[nodiscard]] static std::expected<VideoWriter, Error>
  create(VideoWriterConfig config);

  [[nodiscard]] const std::filesystem::path &output_path() const noexcept;
  [[nodiscard]] PixelBufferFormat format() const noexcept;
  [[nodiscard]] bool cancelled() const noexcept;
  [[nodiscard]] bool finished() const noexcept;

  // Blocks until the input can accept more media, failure, cancel, or stop.
  [[nodiscard]] std::expected<void, Error>
  wait_until_ready(std::stop_token stop = {});

  // Waits for readiness, appends one sealed frame at the exact frame PTS, and
  // consumes the buffer on success. On failure the caller's buffer is
  // unchanged.
  [[nodiscard]] std::expected<void, Error> append(PixelBuffer &buffer,
                                                  core::FrameIndex frame_index,
                                                  std::stop_token stop = {});

  // Marks input finished, cancels on stop, and waits for terminal completion
  // acknowledgement before returning. The owner serializes append/finish;
  // cancel() may wake readiness from another thread, but must never race
  // append. A stuck native API needs a process supervisor; timeout never
  // permits freeing live callback state.
  [[nodiscard]] std::expected<void, Error> finish(std::stop_token stop = {});

  // Cancels writing and wakes readiness waiters. Safe to call repeatedly.
  void cancel() noexcept;

private:
  struct Impl;
  explicit VideoWriter(std::shared_ptr<Impl> impl) noexcept;

  std::shared_ptr<Impl> impl_;
};

struct MediaInspection {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t video_track_count = 0;
  std::uint32_t audio_track_count = 0;
  // Nominal rate reported by AVFoundation (expected 30 for media_30_1).
  double nominal_frame_rate = 0.0;
  // Exact asset duration as a reduced rational in seconds.
  std::int64_t duration_numerator = 0;
  std::int32_t duration_denominator = 1;
  bool h264 = false;
  // ITU-R BT.709 / Rec.709 color tags from the video format description.
  bool rec709_primaries = false;
  bool rec709_transfer = false;
  bool rec709_ycbcr_matrix = false;
  bool playable = false;
};

[[nodiscard]] std::expected<MediaInspection, Error>
inspect_media(const std::filesystem::path &path);

// Sample presentation times extracted from a finished MP4, in decode order of
// the video track. PTS values are exact rationals (seconds).
struct PresentationTimeline {
  std::vector<core::RationalTime> presentation_timestamps;
  core::RationalTime asset_duration{0, 1};
};

// Decodes the video track to per-frame BGRA samples and records presentation
// timestamps. Fails on missing media, non-numeric timing, or duplicate PTS.
[[nodiscard]] std::expected<PresentationTimeline, Error>
extract_presentation_timeline(const std::filesystem::path &path);

// Checks frame count, first/last PTS, exclusive media duration, and that every
// sample matches clock.pts(i) exactly (no duplicates or gaps).
[[nodiscard]] std::expected<void, Error>
validate_presentation_timeline(const PresentationTimeline &timeline,
                               const core::FrameClock &clock,
                               core::FrameIndex expected_frame_count);

} // namespace instantee::spike::apple
