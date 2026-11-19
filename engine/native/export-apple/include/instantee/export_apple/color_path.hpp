#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace instantee::spike::apple {
using diagnostics::Error;
using diagnostics::ExitCode;

// Spike Rec.709 SDR export path. H.264 uses video (limited) range YCbCr with
// ITU-R BT.709 primaries/transfer/matrix. Numeric roundtrip tolerances are
// encode/decode allowances for the spike bitrate, not a certified color grade.
inline constexpr std::uint8_t kRec709RoundtripMaxChannelDelta = 16;

struct Rgba8Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
};

struct Bgra8Color {
  std::uint8_t b = 0;
  std::uint8_t g = 0;
  std::uint8_t r = 0;
  std::uint8_t a = 0;

  [[nodiscard]] friend bool operator==(Bgra8Color,
                                       Bgra8Color) noexcept = default;
};

// Straight-alpha composite over opaque black, then pack opaque BGRA8.
[[nodiscard]] constexpr Bgra8Color
flatten_straight_rgba8_over_black(Rgba8Color rgba) noexcept {
  const auto flatten = [](std::uint8_t channel,
                          std::uint8_t alpha) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(channel) * alpha + 127U) / 255U);
  };
  return Bgra8Color{flatten(rgba.b, rgba.a), flatten(rgba.g, rgba.a),
                    flatten(rgba.r, rgba.a), 255U};
}

// Converts tightly packed straight RGBA8 into opaque BGRA8 over black.
// Destination size must equal source size (width*height*4).
[[nodiscard]] std::expected<void, Error> flatten_straight_rgba8_to_opaque_bgra8(
    std::span<const std::uint8_t> straight_rgba,
    std::span<std::uint8_t> opaque_bgra) noexcept;

[[nodiscard]] inline bool
channel_within_tolerance(std::uint8_t actual, std::uint8_t expected,
                         std::uint8_t max_delta) noexcept {
  const auto delta = actual > expected
                         ? static_cast<std::uint8_t>(actual - expected)
                         : static_cast<std::uint8_t>(expected - actual);
  return delta <= max_delta;
}

[[nodiscard]] inline bool
bgra_within_tolerance(Bgra8Color actual, Bgra8Color expected,
                      std::uint8_t max_delta) noexcept {
  return channel_within_tolerance(actual.b, expected.b, max_delta) &&
         channel_within_tolerance(actual.g, expected.g, max_delta) &&
         channel_within_tolerance(actual.r, expected.r, max_delta) &&
         channel_within_tolerance(actual.a, expected.a, max_delta);
}

// Decoded opaque BGRA8 frame in presentation order.
struct DecodedBgraFrame {
  PixelBufferFormat format{};
  std::vector<std::uint8_t> bgra;
};

// Decodes every video frame to tightly packed 32BGRA. Used by color roundtrip
// checks; presentation timestamps are available via
// extract_presentation_timeline.
[[nodiscard]] std::expected<std::vector<DecodedBgraFrame>, Error>
decode_video_frames_bgra(const std::filesystem::path &path);

// Discrete sample site in pixel coordinates (origin top-left).
struct ColorAccuracySample {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
};

struct ColorAccuracyStats {
  std::uint32_t samples_checked = 0;
  std::uint8_t max_channel_delta = 0;
  std::uint32_t failing_samples = 0;
};

// Compares a direct preview / pre-encode straight RGBA8 frame (display-referred
// Rec.709/sRGB transfer) to one decoded opaque BGRA8 frame after flatten-over-
// black. Fails closed when any sample exceeds `max_channel_delta`.
[[nodiscard]] std::expected<ColorAccuracyStats, Error>
compare_preview_rgba8_to_decoded_bgra8(
    std::span<const std::uint8_t> preview_rgba, std::uint32_t width,
    std::uint32_t height, const DecodedBgraFrame &decoded,
    std::span<const ColorAccuracySample> samples,
    std::uint8_t max_channel_delta = kRec709RoundtripMaxChannelDelta);

} // namespace instantee::spike::apple
