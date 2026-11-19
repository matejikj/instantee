#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::spike::apple {

// Append APIs the Apple export adapter can select between. V1 production uses
// timed CMSampleBuffer append so sample duration stays exact under FrameClock.
enum class WriterAppendApi : std::uint8_t {
  timed_sample_buffer = 1,
  pixel_buffer_adaptor = 2,  // deprecated AVFoundation adaptor
  pixel_buffer_receiver = 3, // modern AVAssetWriter receiver (macOS 26+)
};

[[nodiscard]] constexpr std::string_view
writer_append_api_id(WriterAppendApi api) noexcept {
  switch (api) {
  case WriterAppendApi::timed_sample_buffer:
    return "timed_sample_buffer";
  case WriterAppendApi::pixel_buffer_adaptor:
    return "pixel_buffer_adaptor";
  case WriterAppendApi::pixel_buffer_receiver:
    return "pixel_buffer_receiver";
  }
  return "unknown";
}

// Live macOS/Xcode capability facts for the export adapter. Strings carry
// version text only; no host, user, or hardware UUID identity is recorded.
struct AdapterCompatibilityMatrix {
  std::string macos_product_version;
  std::string macos_build;
  std::string deployment_target;
  std::string selected_sdk_version;
  std::string xcode_version_lock;

  bool h264_mp4_writer_creatable = false;
  bool timed_sample_buffer_supported = false;
  bool pixel_buffer_adaptor_class_present = false;
  bool pixel_buffer_adaptor_deprecated = true;
  bool pixel_buffer_receiver_runtime_available = false;
  bool pixel_buffer_receiver_selected_for_v1 = false;

  WriterAppendApi selected_append_api = WriterAppendApi::timed_sample_buffer;

  // Explicit allow/deny lists required by the hardening acceptance gate.
  std::vector<std::string> supported_configurations;
  std::vector<std::string> unsupported_configurations;
};

// Probes the locked deployment/SDK pair and AVFoundation writer capabilities.
// Never silently claims an unverified append API as selected.
[[nodiscard]] std::expected<AdapterCompatibilityMatrix, diagnostics::Error>
probe_adapter_compatibility();

// Fail-closed selection: unsupported or deprecated APIs return a structured
// error instead of falling back without the caller's knowledge.
[[nodiscard]] std::expected<WriterAppendApi, diagnostics::Error>
select_writer_append_api(WriterAppendApi requested,
                         const AdapterCompatibilityMatrix &matrix);

} // namespace instantee::spike::apple
