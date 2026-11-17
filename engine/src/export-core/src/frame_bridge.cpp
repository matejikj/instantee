#include <instantee/export_core/frame_bridge.hpp>

#include <string_view>
#include <utility>

namespace instantee::export_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "export_core", message};
}

} // namespace

CpuFrameBridge::CpuFrameBridge(std::unique_ptr<VideoSink> sink)
    : sink_(std::move(sink)) {}

std::expected<void, diagnostics::Error>
CpuFrameBridge::publish(EncodableFrame &frame, core::RationalTime pts,
                        std::stop_token stop) {
  if (!sink_) {
    return std::unexpected(
        make_invalid("missing_sink", "CpuFrameBridge requires a VideoSink"));
  }
  return sink_->append(frame, pts, std::move(stop));
}

} // namespace instantee::export_core
