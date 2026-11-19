#include <instantee/export_apple/adapter_compatibility.hpp>
#include <instantee/export_apple/video_writer.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::spike::apple::AdapterCompatibilityMatrix;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::probe_adapter_compatibility;
using instantee::spike::apple::select_writer_append_api;
using instantee::spike::apple::VideoWriter;
using instantee::spike::apple::VideoWriterConfig;
using instantee::spike::apple::writer_append_api_id;
using instantee::spike::apple::WriterAppendApi;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path make_temp_dir() {
  const auto directory =
      std::filesystem::temp_directory_path() / "instantee-adapter-mtx-XXXXXX";
  std::string template_path = directory.string();
  if (mkdtemp(template_path.data()) == nullptr) {
    return {};
  }
  return std::filesystem::path{template_path};
}

[[nodiscard]] std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

void write_matrix_report(const std::filesystem::path &path,
                         const AdapterCompatibilityMatrix &matrix) {
  std::ofstream out(path);
  out << "{\n";
  out << "  \"schema_version\": 1,\n";
  out << "  \"subject\": \"macOS/Xcode adapter compatibility matrix\",\n";
  out << "  \"macos_product_version\": \""
      << json_escape(matrix.macos_product_version) << "\",\n";
  out << "  \"macos_build\": \"" << json_escape(matrix.macos_build) << "\",\n";
  out << "  \"deployment_target\": \"" << json_escape(matrix.deployment_target)
      << "\",\n";
  out << "  \"selected_sdk_version\": \""
      << json_escape(matrix.selected_sdk_version) << "\",\n";
  out << "  \"xcode_version_lock\": \""
      << json_escape(matrix.xcode_version_lock) << "\",\n";
  out << "  \"selected_append_api\": \""
      << writer_append_api_id(matrix.selected_append_api) << "\",\n";
  out << "  \"h264_mp4_writer_creatable\": "
      << (matrix.h264_mp4_writer_creatable ? "true" : "false") << ",\n";
  out << "  \"pixel_buffer_receiver_runtime_available\": "
      << (matrix.pixel_buffer_receiver_runtime_available ? "true" : "false")
      << ",\n";
  out << "  \"supported_configurations\": [\n";
  for (std::size_t i = 0; i < matrix.supported_configurations.size(); ++i) {
    out << "    \"" << json_escape(matrix.supported_configurations[i]) << "\"";
    if (i + 1U < matrix.supported_configurations.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"unsupported_configurations\": [\n";
  for (std::size_t i = 0; i < matrix.unsupported_configurations.size(); ++i) {
    out << "    \"" << json_escape(matrix.unsupported_configurations[i])
        << "\"";
    if (i + 1U < matrix.unsupported_configurations.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  @autoreleasepool {
    const auto directory = make_temp_dir();
    expect(!directory.empty(), "temp dir");
    if (directory.empty()) {
      return EXIT_FAILURE;
    }

    auto matrix = probe_adapter_compatibility();
    expect(matrix.has_value(), "probe matrix");
    if (!matrix) {
      std::cerr << matrix.error().code << ": " << matrix.error().message
                << '\n';
      return EXIT_FAILURE;
    }

    expect(!matrix->deployment_target.empty(), "deployment target");
    expect(!matrix->selected_sdk_version.empty(), "sdk version");
    expect(!matrix->macos_product_version.empty(), "macos version");
    expect(matrix->h264_mp4_writer_creatable, "writer creatable");
    expect(matrix->timed_sample_buffer_supported, "sample buffer supported");
    expect(matrix->selected_append_api == WriterAppendApi::timed_sample_buffer,
           "V1 selects timed sample buffer");
    expect(!matrix->supported_configurations.empty(), "supported listed");
    expect(!matrix->unsupported_configurations.empty(), "unsupported listed");
    expect(matrix->pixel_buffer_adaptor_deprecated, "adaptor deprecated");

    // Selection: supported path accepted; deprecated/modern-but-unbound
    // rejected.
    auto ok =
        select_writer_append_api(WriterAppendApi::timed_sample_buffer, *matrix);
    expect(ok.has_value() && *ok == WriterAppendApi::timed_sample_buffer,
           "select timed sample buffer");

    auto adaptor = select_writer_append_api(
        WriterAppendApi::pixel_buffer_adaptor, *matrix);
    expect(!adaptor.has_value() &&
               adaptor.error().code == "adapter.unsupported_api",
           "adaptor unsupported");

    auto receiver = select_writer_append_api(
        WriterAppendApi::pixel_buffer_receiver, *matrix);
    expect(!receiver.has_value() &&
               receiver.error().code == "adapter.unsupported_api",
           "receiver not selected for V1");

    // Writer failure surfaces: existing path and invalid geometry.
    {
      const auto output = directory / "exists.mp4";
      {
        std::ofstream touch(output);
        touch << "x";
      }
      VideoWriterConfig config;
      config.output_path = output;
      auto writer = VideoWriter::create(config);
      expect(!writer.has_value() && writer.error().code == "output_exists",
             "existing path fails");
    }
    {
      VideoWriterConfig config;
      config.output_path = directory / "bad-geometry.mp4";
      config.format.width = 0;
      config.format.height = 0;
      auto writer = VideoWriter::create(config);
      expect(!writer.has_value() &&
                 writer.error().code == "invalid_writer_config",
             "invalid geometry fails");
    }

    const auto report_path = directory / "matrix-report.json";
    write_matrix_report(report_path, *matrix);
    expect(std::filesystem::exists(report_path), "matrix report written");

    // Optional copy into evidence dir when the test harness provides one.
    if (argc >= 2) {
      const std::filesystem::path evidence_dir = argv[1];
      std::error_code ec;
      std::filesystem::create_directories(evidence_dir, ec);
      std::filesystem::copy_file(
          report_path, evidence_dir / "matrix-report.json",
          std::filesystem::copy_options::overwrite_existing, ec);
      expect(!ec, "copy matrix report to evidence");
    }

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);

    if (failures != 0) {
      std::cerr << failures << " assertion(s) failed\n";
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }
}
