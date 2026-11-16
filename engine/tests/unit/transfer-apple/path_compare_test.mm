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
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple transfer tests require ARC"
#endif

namespace {

using instantee::spike::apple::Bgra8Color;
using instantee::spike::apple::flatten_straight_rgba8_over_black;
using instantee::spike::apple::flatten_straight_rgba8_to_opaque_bgra8;
using instantee::spike::apple::PixelBufferCompatibility;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::metal::Device;
using instantee::spike::metal::expected_test_pattern_pixel;
using instantee::spike::metal::kTestPatternHeight;
using instantee::spike::metal::kTestPatternWidth;
using instantee::spike::metal::TestPatternRenderer;
using instantee::spike::transfer::nearest_rank_latency_ns;
using instantee::spike::transfer::probe_transfer_features;
using instantee::spike::transfer::SharedSurfaceRenderer;

constexpr std::uint32_t kCompareFrames = 8;

[[nodiscard]] Bgra8Color expected_flat(std::uint32_t frame, std::uint32_t x,
                                       std::uint32_t y) noexcept {
  const auto rgba = expected_test_pattern_pixel(frame, x, y);
  return flatten_straight_rgba8_over_black({rgba.r, rgba.g, rgba.b, rgba.a});
}

[[nodiscard]] int verify_bgra(const std::vector<std::uint8_t> &bgra,
                              std::uint32_t frame) {
  for (std::uint32_t y = 0; y < kTestPatternHeight; y += 64) {
    for (std::uint32_t x = 0; x < kTestPatternWidth; x += 64) {
      const std::size_t index =
          (static_cast<std::size_t>(y) * kTestPatternWidth + x) * 4U;
      const Bgra8Color actual{bgra[index], bgra[index + 1U], bgra[index + 2U],
                              bgra[index + 3U]};
      if (actual != expected_flat(frame, x, y)) {
        return 1;
      }
    }
  }
  // Corners and center for denser coverage without scanning every pixel twice.
  const std::uint32_t samples[][2] = {
      {0, 0},
      {kTestPatternWidth - 1U, 0},
      {0, kTestPatternHeight - 1U},
      {kTestPatternWidth - 1U, kTestPatternHeight - 1U},
      {kTestPatternWidth / 2U, kTestPatternHeight / 2U}};
  for (const auto &sample : samples) {
    const std::size_t index =
        (static_cast<std::size_t>(sample[1]) * kTestPatternWidth + sample[0]) *
        4U;
    const Bgra8Color actual{bgra[index], bgra[index + 1U], bgra[index + 2U],
                            bgra[index + 3U]};
    if (actual != expected_flat(frame, sample[0], sample[1])) {
      return 2;
    }
  }
  return 0;
}

[[nodiscard]] int check_quantile_helper() {
  const std::uint64_t samples[] = {10, 20, 30, 40};
  if (nearest_rank_latency_ns(samples, 0.5) != 20 ||
      nearest_rank_latency_ns(samples, 0.99) != 40 ||
      nearest_rank_latency_ns(samples, 0.0) != 10) {
    return 3;
  }
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    if (const int code = check_quantile_helper(); code != 0) {
      return code;
    }

    auto device = Device::open();
    if (!device) {
      return 4;
    }
    auto matrix = probe_transfer_features(*device);
    if (!matrix || !matrix->shared_surface_render) {
      return 5;
    }

    auto staging_renderer = TestPatternRenderer::create(*device);
    auto shared_renderer = SharedSurfaceRenderer::create(*device, 4);
    auto staging_pool =
        PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 4,
                                PixelBufferCompatibility::encoder_cpu_fill);
    if (!staging_renderer || !shared_renderer || !staging_pool) {
      return 6;
    }

    std::vector<std::uint64_t> staging_ns;
    std::vector<std::uint64_t> shared_ns;
    staging_ns.reserve(kCompareFrames);
    shared_ns.reserve(kCompareFrames);

    for (std::uint32_t i = 0; i < kCompareFrames; ++i) {
      const std::uint32_t frame = i & 1U;

      const auto staging_started = std::chrono::steady_clock::now();
      auto rgba = staging_renderer->render(frame);
      if (!rgba) {
        return 7;
      }
      std::vector<std::uint8_t> bgra(rgba->rgba.size());
      if (!flatten_straight_rgba8_to_opaque_bgra8(rgba->rgba, bgra)) {
        return 8;
      }
      auto buffer = staging_pool->acquire();
      if (!buffer || !buffer->fill_bgra8(bgra)) {
        return 9;
      }
      buffer->seal();
      const auto staging_ended = std::chrono::steady_clock::now();
      staging_ns.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(staging_ended -
                                                               staging_started)
              .count()));
      if (const int code = verify_bgra(bgra, frame); code != 0) {
        return 10 + code;
      }

      const auto shared_started = std::chrono::steady_clock::now();
      auto lease = shared_renderer->submit(frame);
      if (!lease) {
        return 20;
      }
      auto shared_bgra = lease->take_bgra8();
      if (!shared_bgra) {
        return 21;
      }
      const auto shared_ended = std::chrono::steady_clock::now();
      shared_ns.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(shared_ended -
                                                               shared_started)
              .count()));
      if (const int code = verify_bgra(*shared_bgra, frame); code != 0) {
        return 22 + code;
      }
    }

    std::sort(staging_ns.begin(), staging_ns.end());
    std::sort(shared_ns.begin(), shared_ns.end());
    if (nearest_rank_latency_ns(staging_ns, 0.5) == 0 ||
        nearest_rank_latency_ns(shared_ns, 0.5) == 0) {
      return 30;
    }
    return 0;
  }
}
