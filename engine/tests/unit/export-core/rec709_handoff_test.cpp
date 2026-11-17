#include <instantee/export_core/rec709_handoff.hpp>
#include <instantee/export_core/video_sink.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stop_token>
#include <vector>

namespace {

using namespace instantee::export_core;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class CaptureSink final : public VideoSink {
public:
  std::expected<void, instantee::diagnostics::Error>
  begin(const VideoSettings &settings) override {
    settings_ = settings;
    begun_ = true;
    return {};
  }

  std::expected<void, instantee::diagnostics::Error>
  append(EncodableFrame &frame, instantee::core::RationalTime,
         std::stop_token) override {
    widths_.push_back(frame.width());
    heights_.push_back(frame.height());
    frames_.push_back(frame.release_pixels());
    return {};
  }

  std::expected<ExportSummary, instantee::diagnostics::Error>
  finish() override {
    finished_ = true;
    return ExportSummary{.frames_appended = frames_.size(),
                         .finished = true,
                         .cancelled = false};
  }

  void cancel() noexcept override { cancelled_ = true; }

  [[nodiscard]] bool begun() const noexcept { return begun_; }
  [[nodiscard]] const std::vector<std::vector<std::byte>> &frames() const {
    return frames_;
  }

private:
  VideoSettings settings_{};
  std::vector<std::vector<std::byte>> frames_{};
  std::vector<std::uint32_t> widths_{};
  std::vector<std::uint32_t> heights_{};
  bool begun_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
};

} // namespace

int main() {
  std::vector<std::uint8_t> rgba{240, 32, 64, 128, 10, 20, 30, 255};
  auto frame =
      make_rec709_encodable_frame(2, 1, rgba, instantee::core::FrameIndex{0});
  expect(frame.has_value(), "make frame");
  if (frame) {
    expect(frame->width() == 2 && frame->height() == 1, "extent");
    auto pixels = frame->release_pixels();
    expect(pixels.size() == 8, "opaque bytes");
    expect(static_cast<std::uint8_t>(pixels[0]) == 120, "flat r0");
    expect(static_cast<std::uint8_t>(pixels[1]) == 16, "flat g0");
    expect(static_cast<std::uint8_t>(pixels[2]) == 32, "flat b0");
    expect(static_cast<std::uint8_t>(pixels[3]) == 255, "flat a0");
    expect(static_cast<std::uint8_t>(pixels[4]) == 10, "opaque r1");
    expect(static_cast<std::uint8_t>(pixels[7]) == 255, "opaque a1");
  }

  expect(!make_rec709_encodable_frame(2, 1, std::span<const std::uint8_t>{},
                                      instantee::core::FrameIndex{0}),
         "reject size mismatch");

  auto sink = std::make_unique<CaptureSink>();
  auto *raw = sink.get();
  CpuFrameBridge bridge(std::move(sink));
  expect(raw->begun() == false, "not begun yet");
  expect(
      bridge.sink().begin(VideoSettings{.width = 2, .height = 1}).has_value(),
      "begin");
  std::stop_source stop;
  expect(publish_rec709_rgba8(
             bridge, 2, 1, rgba, instantee::core::FrameIndex{3},
             instantee::core::RationalTime::zero(), stop.get_token())
             .has_value(),
         "publish");
  expect(raw->frames().size() == 1, "one frame published");
  expect(static_cast<std::uint8_t>(raw->frames()[0][0]) == 120,
         "published flat");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "rec709_handoff_test ok\n";
  return EXIT_SUCCESS;
}
