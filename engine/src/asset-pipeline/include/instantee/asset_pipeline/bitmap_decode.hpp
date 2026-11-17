#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace instantee::asset_pipeline {

enum class BitmapFormat : std::uint8_t {
  png = 1,
  webp = 2,
};

enum class AlphaMode : std::uint8_t {
  // Decoded RGBA with straight (non-premultiplied) alpha in [0,255].
  straight = 1,
  // RGB channels premultiplied by alpha / 255.
  premultiplied = 2,
};

enum class ColorImportPolicy : std::uint8_t {
  // Treat decoded RGB as sRGB. Embedded ICC profiles are ignored (not applied
  // and not retained). Future color-managed import can add an explicit policy.
  assume_srgb_ignore_icc = 1,
};

struct BitmapDecodeLimits {
  std::size_t max_input_bytes = 32U * 1024U * 1024U;
  std::uint32_t max_width_px = 8192;
  std::uint32_t max_height_px = 8192;
  // Reject when width*height exceeds this (checked before pixel allocation).
  std::uint64_t max_pixels = 64U * 1024U * 1024U; // 64 Mpx
};

struct BitmapDecodeOptions {
  AlphaMode alpha_mode = AlphaMode::straight;
  ColorImportPolicy color_policy = ColorImportPolicy::assume_srgb_ignore_icc;
  BitmapDecodeLimits limits{};
};

struct DecodedBitmap {
  BitmapFormat format = BitmapFormat::png;
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  AlphaMode alpha_mode = AlphaMode::straight;
  ColorImportPolicy color_policy = ColorImportPolicy::assume_srgb_ignore_icc;
  // Tight RGBA8 rows, size == width*height*4. No row padding.
  std::vector<std::uint8_t> rgba;
};

// Sniff PNG / WebP magic. Returns nullopt when the prefix is not recognized.
[[nodiscard]] std::optional<BitmapFormat>
sniff_bitmap_format(std::span<const std::uint8_t> bytes) noexcept;

// Read width/height without allocating the pixel buffer. Fails closed on
// truncate/corrupt headers and on dimension/pixel-budget violations.
[[nodiscard]] std::expected<std::pair<std::uint32_t, std::uint32_t>,
                            diagnostics::Error>
probe_bitmap_dimensions(std::span<const std::uint8_t> bytes,
                        const BitmapDecodeLimits &limits = {});

// Bounded PNG/WebP decode to RGBA8. Never touches the GPU. Oversized or
// malformed inputs return Error (invalid_input / resource_budget_exceeded).
[[nodiscard]] std::expected<DecodedBitmap, diagnostics::Error>
decode_bitmap(std::span<const std::uint8_t> bytes,
              const BitmapDecodeOptions &options = {});

} // namespace instantee::asset_pipeline
