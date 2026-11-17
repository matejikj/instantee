#pragma once

#include <instantee/core_model/color.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace instantee::render_core {

// Linear-light working buffer. Channels are premultiplied linear RGB +
// coverage alpha in [0, 1]. Artwork enters as sRGB/straight; output leaves as
// display-referred Rec.709/sRGB-transfer RGBA8 (SDR).
struct LinearPremulImage {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Tightly packed r,g,b,a floats; size == width*height*4.
  std::vector<float> rgba{};
};

struct LinearLayer {
  // Shape color is authored sRGB with straight alpha (DrawOp.color).
  DrawOp shape{};
  // Extra coverage multiplier in [0, 1] (scene opacity).
  float opacity = 1.0f;
};

struct LinearCompositeScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Clear color as authored sRGB/straight (typically opaque).
  Rgba8 clear_color{0, 0, 0, 255};
  std::vector<LinearLayer> layers{};
};

// sRGB/straight byte color → linear premul working color.
[[nodiscard]] core::LinearRgba
srgb8_straight_to_linear_premul(Rgba8 color) noexcept;

// Src-over in linear premul space: out = src + dst * (1 - src.a).
void blend_linear_premul(core::LinearRgba &dst, core::LinearRgba src) noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
clear_linear_premul(LinearPremulImage &image, Rgba8 clear_srgb_straight);

// Composite one shape layer into the working buffer (linear premul src-over).
[[nodiscard]] std::expected<void, diagnostics::Error>
composite_linear_layer(LinearPremulImage &image, const LinearLayer &layer);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_linear_composite_scene(const LinearCompositeScene &scene);

// Clear + layers in order. Working space is linear premul throughout.
[[nodiscard]] std::expected<LinearPremulImage, diagnostics::Error>
rasterize_linear_composite(const LinearCompositeScene &scene);

// Unpremultiply (when a>0) and apply IEC 61966-2-1 / Rec.709 SDR transfer.
// Alpha is preserved as coverage in the output byte.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
encode_rec709_rgba8(const LinearPremulImage &image);

// Flatten straight Rec.709 RGBA8 over opaque black → opaque Rec.709 RGB with
// a=255. Ready for encoder / frame-bridge packaging.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, diagnostics::Error>
flatten_rec709_rgba8_over_black(std::span<const std::uint8_t> straight_rgba);

// Max per-channel delta for CPU goldens comparing linear vs expected encode.
inline constexpr std::uint8_t kLinearCompositorGoldenTolerance = 1;

} // namespace instantee::render_core
