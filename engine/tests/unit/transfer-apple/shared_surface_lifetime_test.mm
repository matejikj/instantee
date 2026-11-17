#include <instantee/export_apple/color_path.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/transfer_apple/shared_surface.hpp>

#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple transfer tests require ARC"
#endif

namespace {

using instantee::diagnostics::ExitCode;
using instantee::render_metal::Device;
using instantee::render_metal::expected_test_pattern_pixel;
using instantee::render_metal::kTestPatternHeight;
using instantee::render_metal::kTestPatternWidth;
using instantee::spike::apple::Bgra8Color;
using instantee::spike::apple::flatten_straight_rgba8_over_black;
using instantee::spike::apple::PixelBuffer;
using instantee::spike::transfer::SharedSurfaceLease;
using instantee::spike::transfer::SharedSurfaceRenderer;

[[nodiscard]] Bgra8Color expected_flat(std::uint32_t frame, std::uint32_t x,
                                       std::uint32_t y) noexcept {
  const auto rgba = expected_test_pattern_pixel(frame, x, y);
  return flatten_straight_rgba8_over_black({rgba.r, rgba.g, rgba.b, rgba.a});
}

[[nodiscard]] int verify_bgra(const std::vector<std::uint8_t> &bgra,
                              std::uint32_t frame) {
  const std::size_t expected_bytes =
      static_cast<std::size_t>(kTestPatternWidth) *
      static_cast<std::size_t>(kTestPatternHeight) * 4U;
  if (bgra.size() != expected_bytes) {
    return 10;
  }
  for (std::uint32_t y = 0; y < kTestPatternHeight; ++y) {
    for (std::uint32_t x = 0; x < kTestPatternWidth; ++x) {
      const std::size_t index =
          (static_cast<std::size_t>(y) * kTestPatternWidth + x) * 4U;
      const Bgra8Color actual{bgra[index], bgra[index + 1U], bgra[index + 2U],
                              bgra[index + 3U]};
      if (actual != expected_flat(frame, x, y)) {
        return 20;
      }
    }
  }
  return 0;
}

[[nodiscard]] int check_delayed_completion(SharedSurfaceRenderer &renderer) {
  auto lease = renderer.submit(0);
  if (!lease || lease->state() != SharedSurfaceLease::State::in_flight) {
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  auto pixels = lease->take_bgra8();
  if (!pixels) {
    return 2;
  }
  if (lease->state() != SharedSurfaceLease::State::consumed) {
    return 3;
  }
  if (const int code = verify_bgra(*pixels, 0); code != 0) {
    return code;
  }
  if (lease->take_bgra8()) {
    return 4;
  }
  return 0;
}

[[nodiscard]] int check_cancel_retirement(SharedSurfaceRenderer &renderer) {
  auto lease = renderer.submit(1);
  if (!lease) {
    return 5;
  }
  lease->cancel();
  if (lease->state() != SharedSurfaceLease::State::cancelled) {
    return 6;
  }
  const auto denied = lease->take_bgra8();
  if (denied || denied.error().exit_code != ExitCode::cancelled ||
      denied.error().code != "cancelled") {
    return 7;
  }
  auto abandoned = renderer.submit(0);
  if (!abandoned) {
    return 8;
  }
  abandoned->cancel();
  return 0;
}

[[nodiscard]] int check_concurrent_leases(SharedSurfaceRenderer &renderer) {
  auto first = renderer.submit(0);
  auto second = renderer.submit(1);
  if (!first || !second) {
    return 9;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto pixels1 = second->take_bgra8();
  auto pixels0 = first->take_bgra8();
  if (!pixels0 || !pixels1) {
    return 11;
  }
  if (const int code = verify_bgra(*pixels0, 0); code != 0) {
    return code;
  }
  if (const int code = verify_bgra(*pixels1, 1); code != 0) {
    return code;
  }
  return 0;
}

[[nodiscard]] int
check_pool_pressure_and_seal(SharedSurfaceRenderer &renderer) {
  if (renderer.capacity() != 2U) {
    return 12;
  }
  auto first = renderer.submit(0);
  auto second = renderer.submit(1);
  if (!first || !second) {
    return 13;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
  auto blocked = renderer.submit(0, {}, deadline);
  if (blocked || blocked.error().code != "deadline_exceeded") {
    return 14;
  }

  {
    auto sealed = first->take_pixel_buffer();
    if (!sealed || !sealed->sealed()) {
      return 15;
    }
    if (sealed->fill_bgra8(std::span<const std::uint8_t>{})) {
      return 16;
    }
  }

  auto third = renderer.submit(0);
  if (!third) {
    return 17;
  }
  auto pixels = third->take_bgra8();
  if (!pixels || verify_bgra(*pixels, 0) != 0) {
    return 18;
  }
  auto second_pixels = second->take_bgra8();
  if (!second_pixels || verify_bgra(*second_pixels, 1) != 0) {
    return 19;
  }
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    auto device = Device::open();
    if (!device) {
      return 30;
    }
    auto renderer = SharedSurfaceRenderer::create(*device, 2);
    if (!renderer) {
      return 31;
    }
    if (const int code = check_delayed_completion(*renderer); code != 0) {
      return code;
    }
    if (const int code = check_cancel_retirement(*renderer); code != 0) {
      return code;
    }
    if (const int code = check_concurrent_leases(*renderer); code != 0) {
      return code;
    }
    if (const int code = check_pool_pressure_and_seal(*renderer); code != 0) {
      return code;
    }
    return 0;
  }
}
