#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <utility>
#include <vector>

namespace instantee::export_core {

// CPU-side frame handed to a VideoSink. Move-only: successful append empties
// the caller's object; failed append leaves bytes intact for retry/cleanup.
class EncodableFrame {
public:
  EncodableFrame() = default;

  [[nodiscard]] static std::expected<EncodableFrame, diagnostics::Error>
  make_rgba8(std::uint32_t width, std::uint32_t height,
             std::vector<std::byte> rgba8, core::FrameIndex frame_index);

  EncodableFrame(EncodableFrame &&other) noexcept;
  EncodableFrame &operator=(EncodableFrame &&other) noexcept;
  ~EncodableFrame() = default;

  EncodableFrame(const EncodableFrame &) = delete;
  EncodableFrame &operator=(const EncodableFrame &) = delete;

  [[nodiscard]] bool empty() const noexcept { return rgba8_.empty(); }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] core::FrameIndex frame_index() const noexcept {
    return frame_index_;
  }
  [[nodiscard]] std::size_t byte_size() const noexcept { return rgba8_.size(); }
  [[nodiscard]] const std::byte *data() const noexcept {
    return rgba8_.empty() ? nullptr : rgba8_.data();
  }

  // Transfers ownership of pixels to the caller; leaves this empty.
  [[nodiscard]] std::vector<std::byte> release_pixels() noexcept;

private:
  EncodableFrame(std::uint32_t width, std::uint32_t height,
                 std::vector<std::byte> rgba8,
                 core::FrameIndex frame_index) noexcept;

  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  core::FrameIndex frame_index_{};
  std::vector<std::byte> rgba8_{};
};

struct VideoSettings {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  core::FrameRate frame_rate = core::FrameRate::media_30_1();
};

struct ExportSummary {
  std::uint64_t frames_appended = 0;
  bool finished = false;
  bool cancelled = false;
};

// Platform-neutral encoder port. Apple adapters implement this behind PImpl;
// render-metal must not call export-apple directly.
class VideoSink {
public:
  virtual ~VideoSink() noexcept = default;

  VideoSink(const VideoSink &) = delete;
  VideoSink &operator=(const VideoSink &) = delete;
  VideoSink(VideoSink &&) noexcept = default;
  VideoSink &operator=(VideoSink &&) noexcept = default;

  [[nodiscard]] virtual std::expected<void, diagnostics::Error>
  begin(const VideoSettings &settings) = 0;

  // On success, takes ownership by emptying `frame`. On error, leaves `frame`
  // unchanged. Acceptance is not encode completion.
  [[nodiscard]] virtual std::expected<void, diagnostics::Error>
  append(EncodableFrame &frame, core::RationalTime pts,
         std::stop_token stop) = 0;

  [[nodiscard]] virtual std::expected<ExportSummary, diagnostics::Error>
  finish() = 0;

  // Best-effort abort. Safe to call multiple times; does not throw.
  virtual void cancel() noexcept = 0;

protected:
  VideoSink() = default;
};

} // namespace instantee::export_core
