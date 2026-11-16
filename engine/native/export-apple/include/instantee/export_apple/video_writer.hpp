#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <stop_token>

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

  // Marks the input finished and completes writing. Verifies writer status.
  [[nodiscard]] std::expected<void, Error> finish();

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
  bool playable = false;
};

[[nodiscard]] std::expected<MediaInspection, Error>
inspect_media(const std::filesystem::path &path);

} // namespace instantee::spike::apple
