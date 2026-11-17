#include <instantee/export_core/frame_bridge.hpp>
#include <instantee/export_core/video_sink.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

using namespace instantee::core;
using namespace instantee::diagnostics;
using namespace instantee::export_core;

// Test double: records ownership transfers and exercises error/cancel paths.
class FakeVideoSink final : public VideoSink {
public:
  std::expected<void, Error> begin(const VideoSettings &settings) override {
    if (begun_ && !finished_ && !cancelled_) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "already_begun",
                                   "fake_sink", "begin called twice"});
    }
    if (settings.width == 0 || settings.height == 0 ||
        !settings.frame_rate.valid()) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "invalid_settings",
                                   "fake_sink", "bad video settings"});
    }
    settings_ = settings;
    begun_ = true;
    finished_ = false;
    cancelled_ = false;
    frames_.clear();
    return {};
  }

  std::expected<void, Error> append(EncodableFrame &frame, RationalTime pts,
                                    std::stop_token stop) override {
    if (!begun_ || finished_ || cancelled_) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "not_recording",
                                   "fake_sink", "append outside begin/finish"});
    }
    if (stop.stop_requested()) {
      return std::unexpected(Error{ExitCode::cancelled, ErrorKind::cancelled,
                                   "cancelled", "fake_sink",
                                   "append observed stop"});
    }
    if (fail_next_append_) {
      fail_next_append_ = false;
      // Ownership must remain with the caller on error.
      return std::unexpected(Error{ExitCode::export_failure,
                                   ErrorKind::export_failure, "append_failed",
                                   "fake_sink", "forced append failure"});
    }
    if (frame.empty()) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "empty_frame",
                                   "fake_sink", "empty frame"});
    }
    if (frame.width() != settings_.width ||
        frame.height() != settings_.height) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "size_mismatch",
                                   "fake_sink", "frame size mismatch"});
    }
    Recorded recorded;
    recorded.pts = pts;
    recorded.frame_index = frame.frame_index();
    recorded.bytes = frame.release_pixels();
    frames_.push_back(std::move(recorded));
    return {};
  }

  std::expected<ExportSummary, Error> finish() override {
    if (!begun_) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "not_begun",
                                   "fake_sink", "finish without begin"});
    }
    if (cancelled_) {
      return std::unexpected(Error{ExitCode::cancelled, ErrorKind::cancelled,
                                   "cancelled", "fake_sink",
                                   "finish after cancel"});
    }
    if (finished_) {
      return std::unexpected(Error{ExitCode::invalid_input,
                                   ErrorKind::invalid_input, "already_finished",
                                   "fake_sink", "finish called twice"});
    }
    finished_ = true;
    return ExportSummary{.frames_appended = frames_.size(),
                         .finished = true,
                         .cancelled = false};
  }

  void cancel() noexcept override {
    cancelled_ = true;
    ++cancel_count_;
  }

  void fail_next_append() { fail_next_append_ = true; }

  struct Recorded {
    RationalTime pts{};
    FrameIndex frame_index{};
    std::vector<std::byte> bytes;
  };

  [[nodiscard]] const std::vector<Recorded> &frames() const noexcept {
    return frames_;
  }
  [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
  [[nodiscard]] int cancel_count() const noexcept { return cancel_count_; }

private:
  VideoSettings settings_{};
  bool begun_ = false;
  bool finished_ = false;
  bool cancelled_ = false;
  bool fail_next_append_ = false;
  int cancel_count_ = 0;
  std::vector<Recorded> frames_;
};

[[nodiscard]] EncodableFrame make_solid(std::uint32_t w, std::uint32_t h,
                                        FrameIndex index) {
  std::vector<std::byte> bytes(static_cast<std::size_t>(w) * h * 4U,
                               std::byte{0x7f});
  auto frame = EncodableFrame::make_rgba8(w, h, std::move(bytes), index);
  expect(frame.has_value(), "make solid frame");
  return std::move(*frame);
}

} // namespace

int main() {
  auto pts = RationalTime::make(0, 1);
  expect(pts.has_value(), "pts 0");

  // Ownership: success empties caller frame; failure preserves it.
  {
    auto sink = std::make_unique<FakeVideoSink>();
    FakeVideoSink *raw = sink.get();
    CpuFrameBridge bridge(std::move(sink));
    VideoSettings settings{.width = 2, .height = 2};
    expect(raw->begin(settings).has_value(), "begin");

    EncodableFrame frame = make_solid(2, 2, FrameIndex{0});
    expect(!frame.empty(), "frame populated");
    expect(bridge.publish(frame, *pts, {}).has_value(), "publish ok");
    expect(frame.empty(), "success takes ownership");
    expect(raw->frames().size() == 1, "one recorded frame");
    expect(raw->frames()[0].bytes.size() == 16, "rgba8 retained by sink");

    EncodableFrame retry = make_solid(2, 2, FrameIndex{1});
    raw->fail_next_append();
    auto failed = bridge.publish(retry, *pts, {});
    expect(!failed.has_value(), "forced append failure");
    expect(!retry.empty(), "failure leaves ownership with caller");
    expect(raw->frames().size() == 1, "failed append not recorded");

    expect(bridge.publish(retry, *pts, {}).has_value(), "retry succeeds");
    expect(retry.empty(), "retry transfers ownership");

    auto summary = raw->finish();
    expect(summary.has_value() && summary->frames_appended == 2,
           "finish summary");
  }

  // Cancel / stop paths.
  {
    auto sink = std::make_unique<FakeVideoSink>();
    FakeVideoSink *raw = sink.get();
    expect(raw->begin(VideoSettings{.width = 2, .height = 2}).has_value(),
           "begin cancel case");
    raw->cancel();
    expect(raw->cancel_count() == 1, "cancel counted");
    EncodableFrame frame = make_solid(2, 2, FrameIndex{0});
    expect(!raw->append(frame, *pts, {}).has_value(),
           "append rejected after cancel");
    expect(!frame.empty(), "cancel path keeps caller ownership");
    expect(!raw->finish().has_value(), "finish rejected after cancel");
    raw->cancel();
    expect(raw->cancel_count() == 2, "cancel is idempotent");
  }

  {
    auto sink = std::make_unique<FakeVideoSink>();
    FakeVideoSink *raw = sink.get();
    expect(raw->begin(VideoSettings{.width = 2, .height = 2}).has_value(),
           "begin stop case");
    std::stop_source source;
    source.request_stop();
    EncodableFrame frame = make_solid(2, 2, FrameIndex{0});
    expect(!raw->append(frame, *pts, source.get_token()).has_value(),
           "append observes stop");
    expect(!frame.empty(), "stop leaves ownership with caller");
  }

  // Size mismatch and invalid construction.
  {
    expect(!EncodableFrame::make_rgba8(0, 1, {}, FrameIndex{0}).has_value(),
           "reject zero extent");
    expect(!EncodableFrame::make_rgba8(2, 2, {std::byte{1}}, FrameIndex{0})
                .has_value(),
           "reject wrong byte size");
    auto sink = std::make_unique<FakeVideoSink>();
    FakeVideoSink *raw = sink.get();
    expect(raw->begin(VideoSettings{.width = 2, .height = 2}).has_value(),
           "begin mismatch");
    EncodableFrame wrong = make_solid(4, 4, FrameIndex{0});
    expect(!raw->append(wrong, *pts, {}).has_value(), "size mismatch");
    expect(!wrong.empty(), "mismatch keeps ownership");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
