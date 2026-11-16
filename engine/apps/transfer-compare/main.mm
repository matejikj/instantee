#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/test_pattern.hpp>
#include <instantee/transfer_apple/feature_matrix.hpp>
#include <instantee/transfer_apple/shared_surface.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple transfer compare requires ARC"
#endif

namespace {

using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::PixelBufferCompatibility;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::metal::Device;
using instantee::spike::metal::TestPatternRenderer;
using instantee::spike::transfer::nearest_rank_latency_ns;
using instantee::spike::transfer::probe_transfer_features;
using instantee::spike::transfer::SharedSurfaceRenderer;
using instantee::spike::transfer::TransferFeatureMatrix;

struct Options {
  std::uint32_t frames = 60;
  std::uint32_t capacity = 3;
  std::string output_path;
};

[[nodiscard]] bool parse_u32(std::string_view text, std::uint32_t &out) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const unsigned long value = std::strtoul(std::string{text}.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' ||
      value > static_cast<unsigned long>(
                  std::numeric_limits<std::uint32_t>::max())) {
    return false;
  }
  out = static_cast<std::uint32_t>(value);
  return true;
}

[[nodiscard]] int parse_args(int argc, char **argv, Options &options) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--help") {
      std::cout
          << "spike-transfer-compare --frames N --capacity E --output "
             "path.json\n"
          << "Isolated CPU-staging vs IOSurface-shared transfer timing. Not a\n"
          << "certified B-00 throughput or p99 export measurement.\n";
      return 0;
    }
    if (arg == "--frames" && i + 1 < argc) {
      if (!parse_u32(argv[++i], options.frames) || options.frames == 0U) {
        return 2;
      }
      continue;
    }
    if (arg == "--capacity" && i + 1 < argc) {
      if (!parse_u32(argv[++i], options.capacity) || options.capacity == 0U) {
        return 2;
      }
      continue;
    }
    if (arg == "--output" && i + 1 < argc) {
      options.output_path = argv[++i];
      continue;
    }
    return 2;
  }
  if (options.output_path.empty()) {
    return 2;
  }
  return -1;
}

[[nodiscard]] std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    default:
      out += ch;
      break;
    }
  }
  return out;
}

[[nodiscard]] std::string quantile_object(std::vector<std::uint64_t> samples) {
  std::sort(samples.begin(), samples.end());
  return std::string{"{\"count\":"} + std::to_string(samples.size()) +
         ",\"p50_ns\":" +
         std::to_string(nearest_rank_latency_ns(samples, 0.5)) +
         ",\"p95_ns\":" +
         std::to_string(nearest_rank_latency_ns(samples, 0.95)) +
         ",\"p99_ns\":" +
         std::to_string(nearest_rank_latency_ns(samples, 0.99)) + "}";
}

void append_feature_matrix(std::ostream &out,
                           const TransferFeatureMatrix &matrix) {
  out << "\"feature_matrix\":{"
      << "\"metal_device_available\":"
      << (matrix.metal_device_available ? "true" : "false") << ","
      << "\"apple7_family\":" << (matrix.apple7_family ? "true" : "false")
      << ","
      << "\"metal3\":" << (matrix.metal3 ? "true" : "false") << ","
      << "\"unified_memory\":" << (matrix.unified_memory ? "true" : "false")
      << ","
      << "\"low_power\":" << (matrix.low_power ? "true" : "false") << ","
      << "\"headless\":" << (matrix.headless ? "true" : "false") << ","
      << "\"cv_metal_texture_cache\":"
      << (matrix.cv_metal_texture_cache ? "true" : "false") << ","
      << "\"metal_compatible_pixel_buffer\":"
      << (matrix.metal_compatible_pixel_buffer ? "true" : "false") << ","
      << "\"iosurface_backed_pixel_buffer\":"
      << (matrix.iosurface_backed_pixel_buffer ? "true" : "false") << ","
      << "\"shared_surface_render\":"
      << (matrix.shared_surface_render ? "true" : "false") << ","
      << "\"recommended_max_working_set_bytes\":"
      << matrix.recommended_max_working_set_bytes << ","
      << "\"device_name\":\"" << json_escape(matrix.device_name) << "\","
      << "\"device_architecture\":\"" << json_escape(matrix.device_architecture)
      << "\","
      << "\"macos_product_version\":\""
      << json_escape(matrix.macos_product_version) << "\","
      << "\"macos_build\":\"" << json_escape(matrix.macos_build) << "\","
      << "\"selected_sdk_version\":\""
      << json_escape(matrix.selected_sdk_version) << "\","
      << "\"deployment_target\":\"" << json_escape(matrix.deployment_target)
      << "\"}";
}

} // namespace

int main(int argc, char **argv) {
  @autoreleasepool {
    Options options;
    const int parsed = parse_args(argc, argv, options);
    if (parsed >= 0) {
      return parsed;
    }

    auto device = Device::open();
    if (!device) {
      return 5;
    }
    auto matrix = probe_transfer_features(*device);
    if (!matrix) {
      return 6;
    }

    auto staging_renderer = TestPatternRenderer::create(*device);
    auto shared_renderer =
        SharedSurfaceRenderer::create(*device, options.capacity);
    auto staging_pool = PixelBufferPool::create(
        PixelBufferFormat::media_1080x1920(), options.capacity,
        PixelBufferCompatibility::encoder_cpu_fill);
    if (!staging_renderer || !shared_renderer || !staging_pool) {
      return 7;
    }

    std::vector<std::uint64_t> staging_ns;
    std::vector<std::uint64_t> shared_ns;
    staging_ns.reserve(options.frames);
    shared_ns.reserve(options.frames);

    for (std::uint32_t i = 0; i < options.frames; ++i) {
      const std::uint32_t frame = i & 1U;

      const auto staging_started = std::chrono::steady_clock::now();
      auto rgba = staging_renderer->render(frame);
      if (!rgba) {
        return 8;
      }
      std::vector<std::uint8_t> bgra(rgba->rgba.size());
      if (!flatten_straight_rgba8_to_opaque_bgra8(rgba->rgba, bgra)) {
        return 9;
      }
      auto buffer = staging_pool->acquire();
      if (!buffer || !buffer->fill_bgra8(bgra)) {
        return 10;
      }
      buffer->seal();
      const auto staging_ended = std::chrono::steady_clock::now();
      staging_ns.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(staging_ended -
                                                               staging_started)
              .count()));

      const auto shared_started = std::chrono::steady_clock::now();
      auto lease = shared_renderer->submit(frame);
      if (!lease) {
        return 11;
      }
      auto shared_buffer = lease->take_pixel_buffer();
      if (!shared_buffer || !shared_buffer->sealed()) {
        return 12;
      }
      const auto shared_ended = std::chrono::steady_clock::now();
      shared_ns.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(shared_ended -
                                                               shared_started)
              .count()));
    }

    std::ofstream out{options.output_path, std::ios::binary | std::ios::trunc};
    if (!out) {
      return 13;
    }
    out << "{"
        << "\"schema\":\"instantee.transfer_compare.v1\","
        << "\"certified\":false,"
        << "\"validation\":\"" << INSTANTEE_TRANSFER_COMPARE_VALIDATION << "\","
        << "\"frames\":" << options.frames << ","
        << "\"pool_capacity\":" << options.capacity << ","
        << "\"units\":{\"latency\":\"nanoseconds\"},";
    append_feature_matrix(out, *matrix);
    out << ",\"cpu_staging\":" << quantile_object(staging_ns)
        << ",\"iosurface_shared\":" << quantile_object(shared_ns) << "}\n";
    if (!out.good()) {
      return 14;
    }
    return 0;
  }
}
