#pragma once

#include <instantee/export_core/video_sink.hpp>

#include <memory>

namespace instantee::export_core {

// Frame bridge keeps render backends from linking encoder adapters. It accepts
// CPU EncodableFrame values and forwards them to a VideoSink. Borrowed pixel
// views are intentionally unsupported: ownership must be explicit and
// move-only.
class FrameBridge {
public:
  virtual ~FrameBridge() noexcept = default;

  FrameBridge(const FrameBridge &) = delete;
  FrameBridge &operator=(const FrameBridge &) = delete;
  FrameBridge(FrameBridge &&) noexcept = default;
  FrameBridge &operator=(FrameBridge &&) noexcept = default;

  [[nodiscard]] virtual std::expected<void, diagnostics::Error>
  publish(EncodableFrame &frame, core::RationalTime pts,
          std::stop_token stop) = 0;

protected:
  FrameBridge() = default;
};

// Owns a VideoSink and forwards publish → append. Lifetime: bridge outlives
// in-flight publish calls; sink is destroyed with the bridge.
class CpuFrameBridge final : public FrameBridge {
public:
  explicit CpuFrameBridge(std::unique_ptr<VideoSink> sink);

  [[nodiscard]] std::expected<void, diagnostics::Error>
  publish(EncodableFrame &frame, core::RationalTime pts,
          std::stop_token stop) override;

  [[nodiscard]] VideoSink &sink() noexcept { return *sink_; }
  [[nodiscard]] const VideoSink &sink() const noexcept { return *sink_; }

private:
  std::unique_ptr<VideoSink> sink_;
};

} // namespace instantee::export_core
