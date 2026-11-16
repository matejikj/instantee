#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace instantee::spike::metal {

inline constexpr std::uint32_t kTestPatternWidth = 1080;
inline constexpr std::uint32_t kTestPatternHeight = 1920;
inline constexpr std::uint32_t kTestPatternChannels = 4;

struct Extent2u {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// Tightly packed row-major straight RGBA8 bytes. Size is width*height*4.
struct Rgba8Frame {
  Extent2u extent{};
  std::vector<std::uint8_t> rgba;

  [[nodiscard]] bool valid_dimensions() const noexcept {
    return extent.width == kTestPatternWidth &&
           extent.height == kTestPatternHeight &&
           rgba.size() == static_cast<std::size_t>(kTestPatternWidth) *
                              static_cast<std::size_t>(kTestPatternHeight) *
                              kTestPatternChannels;
  }
};

struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;

  [[nodiscard]] friend bool operator==(Rgba8 lhs, Rgba8 rhs) noexcept {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
  }
};

// CPU oracle that matches engine/shaders/spike/test_pattern.metal.
[[nodiscard]] constexpr Rgba8
expected_test_pattern_pixel(std::uint32_t frame_index, std::uint32_t x,
                            std::uint32_t y) noexcept {
  const std::uint8_t base_r = frame_index == 0 ? 240 : 16;
  const std::uint8_t base_g = frame_index == 0 ? 32 : 200;
  const std::uint8_t base_b = frame_index == 0 ? 64 : 96;
  const std::uint8_t base_a = frame_index == 0 ? 128 : 200;
  return Rgba8{static_cast<std::uint8_t>((base_r + (x & 15u)) & 255u),
               static_cast<std::uint8_t>((base_g + (y & 15u)) & 255u), base_b,
               base_a};
}

[[nodiscard]] inline Rgba8 frame_pixel(const Rgba8Frame &frame, std::uint32_t x,
                                       std::uint32_t y) noexcept {
  const std::size_t index =
      (static_cast<std::size_t>(y) * frame.extent.width + x) *
      kTestPatternChannels;
  return Rgba8{frame.rgba[index], frame.rgba[index + 1], frame.rgba[index + 2],
               frame.rgba[index + 3]};
}

} // namespace instantee::spike::metal
