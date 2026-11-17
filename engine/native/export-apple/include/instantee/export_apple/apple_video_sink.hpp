#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/export_core/video_sink.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>

namespace instantee::export_apple {

// VideoSink adapter over AVFoundation VideoWriter. Domain/export-core callers
// never see AVFoundation types; pixels enter as EncodableFrame RGBA8.
class AppleVideoSink final : public export_core::VideoSink {
public:
  AppleVideoSink(AppleVideoSink &&) noexcept;
  AppleVideoSink &operator=(AppleVideoSink &&) noexcept;
  ~AppleVideoSink() override;

  AppleVideoSink(const AppleVideoSink &) = delete;
  AppleVideoSink &operator=(const AppleVideoSink &) = delete;

  // `output_path` must not exist yet (AVAssetWriter requirement). Typically the
  // OutputTransaction staging file path.
  [[nodiscard]] static std::expected<std::unique_ptr<AppleVideoSink>,
                                     diagnostics::Error>
  create(std::filesystem::path output_path, std::uint32_t pool_capacity = 4);

  [[nodiscard]] const std::filesystem::path &output_path() const noexcept;

  [[nodiscard]] std::expected<void, diagnostics::Error>
  begin(const export_core::VideoSettings &settings) override;

  [[nodiscard]] std::expected<void, diagnostics::Error>
  append(export_core::EncodableFrame &frame, core::RationalTime pts,
         std::stop_token stop) override;

  [[nodiscard]] std::expected<export_core::ExportSummary, diagnostics::Error>
  finish() override;

  void cancel() noexcept override;

private:
  struct State;
  explicit AppleVideoSink(std::unique_ptr<State> state) noexcept;

  std::unique_ptr<State> state_;
};

} // namespace instantee::export_apple
