#include <instantee/export_apple/adapter_compatibility.hpp>

#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <AVFoundation/AVFoundation.h>
#include <Foundation/Foundation.h>

#include <sys/sysctl.h>

#include <filesystem>
#include <format>
#include <string>
#include <unistd.h>

#if !__has_feature(objc_arc)
#error "The Apple export adapter requires ARC"
#endif

namespace instantee::spike::apple {
namespace {

using diagnostics::Error;
using diagnostics::ErrorKind;
using diagnostics::ExitCode;

constexpr const char *kPhase = "adapter_compatibility";

[[nodiscard]] Error make_invalid(std::string_view code,
                                 std::string_view message) {
  return Error{ExitCode::invalid_input, ErrorKind::invalid_input, code, kPhase,
               message};
}

[[nodiscard]] std::string sysctl_string(const char *name) noexcept {
  char buffer[128];
  std::size_t size = sizeof(buffer);
  if (sysctlbyname(name, buffer, &size, nullptr, 0) != 0 || size == 0U) {
    return {};
  }
  if (buffer[size - 1U] == '\0') {
    return std::string{buffer};
  }
  return std::string{buffer, size};
}

[[nodiscard]] std::filesystem::path make_probe_mp4_path() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-adapter-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path} / "probe.mp4";
}

} // namespace

std::expected<AdapterCompatibilityMatrix, Error> probe_adapter_compatibility() {
  AdapterCompatibilityMatrix matrix;
  matrix.macos_build = sysctl_string("kern.osversion");
  matrix.deployment_target = INSTANTEE_EXPORT_DEPLOYMENT_TARGET;
  matrix.selected_sdk_version = INSTANTEE_EXPORT_SDK_VERSION;
  matrix.xcode_version_lock = INSTANTEE_EXPORT_XCODE_VERSION_LOCK;

  @autoreleasepool {
    const NSOperatingSystemVersion version =
        NSProcessInfo.processInfo.operatingSystemVersion;
    matrix.macos_product_version =
        std::format("{}.{}.{}", version.majorVersion, version.minorVersion,
                    version.patchVersion);

    // Modern PixelBufferReceiver is documented from macOS 26; the ObjC++ V1
    // writer does not bind it yet, but capability detection must report truth.
    if (@available(macOS 26.0, *)) {
      matrix.pixel_buffer_receiver_runtime_available = true;
    }

    matrix.pixel_buffer_adaptor_class_present =
        [AVAssetWriterInputPixelBufferAdaptor class] != nil;
    matrix.pixel_buffer_adaptor_deprecated = true;
  }

  // V1 path: timed sample buffers remain supported on the locked ObjC API.
  matrix.timed_sample_buffer_supported = true;
  matrix.pixel_buffer_receiver_selected_for_v1 = false;
  matrix.selected_append_api = WriterAppendApi::timed_sample_buffer;

  const auto probe_path = make_probe_mp4_path();
  if (!probe_path.empty()) {
    VideoWriterConfig config;
    config.output_path = probe_path;
    config.format = PixelBufferFormat::media_1080x1920();
    auto writer = VideoWriter::create(config);
    matrix.h264_mp4_writer_creatable = writer.has_value();
    if (writer) {
      writer->cancel();
    }
    std::error_code ec;
    std::filesystem::remove_all(probe_path.parent_path(), ec);
  }

  matrix.supported_configurations = {
      "writer_append=timed_sample_buffer;codec=h264;file=mp4;audio=none;"
      "color=rec709-sdr;geometry=1080x1920;rate=30/1",
      "deployment_target=" + matrix.deployment_target +
          ";sdk=" + matrix.selected_sdk_version,
  };

  matrix.unsupported_configurations = {
      "writer_append=pixel_buffer_adaptor (deprecated; not selected for V1)",
  };
  if (matrix.pixel_buffer_receiver_runtime_available) {
    matrix.unsupported_configurations.push_back(
        "writer_append=pixel_buffer_receiver (runtime available; ObjC++ V1 "
        "binding not selected — use timed_sample_buffer)");
  } else {
    matrix.unsupported_configurations.push_back(
        "writer_append=pixel_buffer_receiver (requires macOS 26+ runtime)");
  }
  matrix.unsupported_configurations.push_back(
      "writer_append=any;audio=present (video-only export)");
  matrix.unsupported_configurations.push_back(
      "writer_output_path_exists (AVAssetWriter requires a new path)");

  if (matrix.deployment_target.empty() || matrix.selected_sdk_version.empty()) {
    return std::unexpected(make_invalid(
        "adapter.matrix_incomplete",
        "Deployment target or SDK version lock is missing from the build"));
  }
  if (!matrix.h264_mp4_writer_creatable) {
    return std::unexpected(
        Error{ExitCode::export_failure, ErrorKind::export_failure,
              "adapter.writer_unavailable", kPhase,
              "H.264 MP4 AVAssetWriter could not be created on this host"});
  }
  return matrix;
}

std::expected<WriterAppendApi, Error>
select_writer_append_api(WriterAppendApi requested,
                         const AdapterCompatibilityMatrix &matrix) {
  switch (requested) {
  case WriterAppendApi::timed_sample_buffer:
    if (!matrix.timed_sample_buffer_supported) {
      return std::unexpected(make_invalid(
          "adapter.unsupported_api",
          "timed_sample_buffer is not supported on this configuration"));
    }
    return WriterAppendApi::timed_sample_buffer;
  case WriterAppendApi::pixel_buffer_adaptor:
    return std::unexpected(make_invalid(
        "adapter.unsupported_api",
        "pixel_buffer_adaptor is deprecated and unsupported for V1 selection"));
  case WriterAppendApi::pixel_buffer_receiver:
    if (!matrix.pixel_buffer_receiver_runtime_available) {
      return std::unexpected(make_invalid(
          "adapter.unsupported_api",
          "pixel_buffer_receiver requires macOS 26+ and is unavailable here"));
    }
    // Runtime can see the modern API, but the ObjC++ writer has not adopted it.
    return std::unexpected(make_invalid(
        "adapter.unsupported_api",
        "pixel_buffer_receiver is detected but not selected for the ObjC++ V1 "
        "writer; request timed_sample_buffer"));
  }
  return std::unexpected(
      make_invalid("adapter.unknown_api", "Unknown writer append API request"));
}

} // namespace instantee::spike::apple
