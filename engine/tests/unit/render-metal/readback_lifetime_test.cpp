#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/frame.hpp>
#include <instantee/render_metal/readback.hpp>
#include <instantee/render_metal/test_pattern.hpp>

#include <chrono>
#include <thread>
#include <utility>

namespace {

using instantee::diagnostics::ExitCode;
using instantee::render_metal::Device;
using instantee::render_metal::expected_test_pattern_pixel;
using instantee::render_metal::frame_pixel;
using instantee::render_metal::kTestPatternHeight;
using instantee::render_metal::kTestPatternWidth;
using instantee::render_metal::ReadbackLease;
using instantee::render_metal::Rgba8Frame;
using instantee::render_metal::TestPatternRenderer;

[[nodiscard]] int verify_frame(const Rgba8Frame &frame, std::uint32_t index) {
  if (!frame.valid_dimensions()) {
    return 10 + static_cast<int>(index);
  }
  for (std::uint32_t y = 0; y < kTestPatternHeight; ++y) {
    for (std::uint32_t x = 0; x < kTestPatternWidth; ++x) {
      if (frame_pixel(frame, x, y) !=
          expected_test_pattern_pixel(index, x, y)) {
        return 20 + static_cast<int>(index);
      }
    }
  }
  return 0;
}

[[nodiscard]] int check_delayed_completion(TestPatternRenderer &renderer) {
  auto lease = renderer.submit(0);
  if (!lease || lease->state() != ReadbackLease::State::in_flight) {
    return 1;
  }

  // Lease outlives the submit frame and completes after an intentional delay.
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  auto pixels = lease->take_pixels();
  if (!pixels) {
    return 2;
  }
  if (lease->state() != ReadbackLease::State::consumed) {
    return 3;
  }
  if (const int code = verify_frame(*pixels, 0); code != 0) {
    return code;
  }
  if (lease->take_pixels()) {
    return 4;
  }
  return 0;
}

[[nodiscard]] int check_cancel_retirement(TestPatternRenderer &renderer) {
  auto lease = renderer.submit(1);
  if (!lease) {
    return 5;
  }
  lease->cancel();
  if (lease->state() != ReadbackLease::State::cancelled) {
    return 6;
  }
  const auto denied = lease->take_pixels();
  if (denied || denied.error().exit_code != ExitCode::cancelled ||
      denied.error().code != "cancelled") {
    return 7;
  }
  // Destroying a cancelled in-flight lease must retire GPU work without UAF.
  auto abandoned = renderer.submit(0);
  if (!abandoned) {
    return 32;
  }
  abandoned->cancel();
  return 0;
}

[[nodiscard]] int check_concurrent_leases(TestPatternRenderer &renderer) {
  auto first = renderer.submit(0);
  auto second = renderer.submit(1);
  if (!first || !second) {
    return 8;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  auto pixels1 = second->take_pixels();
  auto pixels0 = first->take_pixels();
  if (!pixels0 || !pixels1) {
    return 9;
  }
  if (const int code = verify_frame(*pixels0, 0); code != 0) {
    return code;
  }
  if (const int code = verify_frame(*pixels1, 1); code != 0) {
    return code;
  }
  return 0;
}

[[nodiscard]] int check_moved_lease_delay(TestPatternRenderer &renderer) {
  auto launched = renderer.submit(1);
  if (!launched) {
    return 30;
  }
  ReadbackLease moved = std::move(*launched);
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  auto pixels = moved.take_pixels();
  if (!pixels) {
    return 31;
  }
  return verify_frame(*pixels, 1);
}

} // namespace

int main() {
  auto device = Device::open();
  if (!device) {
    return 40;
  }
  auto renderer = TestPatternRenderer::create(*device);
  if (!renderer) {
    return 41;
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
  return check_moved_lease_delay(*renderer);
}
