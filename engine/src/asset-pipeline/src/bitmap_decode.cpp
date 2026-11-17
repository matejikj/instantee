#include <instantee/asset_pipeline/bitmap_decode.hpp>

#include <stb_image.h>
#include <webp/decode.h>

#include <cstring>
#include <limits>
#include <string_view>

namespace instantee::asset_pipeline {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "asset_pipeline", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "asset_pipeline", message};
}

[[nodiscard]] bool overflow_mul(std::uint32_t a, std::uint32_t b,
                                std::uint64_t &out) noexcept {
  out = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
  return a != 0 && out / a != b;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
check_dimensions(std::uint32_t width, std::uint32_t height,
                 const BitmapDecodeLimits &limits) {
  if (width == 0 || height == 0) {
    return std::unexpected(make_invalid("bitmap.dimensions.zero",
                                        "bitmap dimensions must be > 0"));
  }
  if (width > limits.max_width_px || height > limits.max_height_px) {
    return std::unexpected(
        make_budget("bitmap.dimensions.limit",
                    "bitmap width/height exceeds configured max"));
  }
  std::uint64_t pixels = 0;
  if (overflow_mul(width, height, pixels) || pixels > limits.max_pixels) {
    return std::unexpected(make_budget(
        "bitmap.pixels.limit", "bitmap pixel count exceeds configured max"));
  }
  return {};
}

[[nodiscard]] std::uint32_t read_be32(const std::uint8_t *p) noexcept {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

[[nodiscard]] std::expected<std::pair<std::uint32_t, std::uint32_t>,
                            diagnostics::Error>
probe_png(std::span<const std::uint8_t> bytes) {
  // signature (8) + IHDR length(4)+type(4)+data(13)+crc(4) = 33
  if (bytes.size() < 33) {
    return std::unexpected(
        make_invalid("bitmap.png.truncated", "PNG header truncated"));
  }
  static constexpr std::uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  if (std::memcmp(bytes.data(), kSig, 8) != 0) {
    return std::unexpected(
        make_invalid("bitmap.png.signature", "PNG signature mismatch"));
  }
  const std::uint32_t length = read_be32(bytes.data() + 8);
  if (length < 13 || std::memcmp(bytes.data() + 12, "IHDR", 4) != 0) {
    return std::unexpected(
        make_invalid("bitmap.png.ihdr", "PNG IHDR chunk missing"));
  }
  const std::uint32_t width = read_be32(bytes.data() + 16);
  const std::uint32_t height = read_be32(bytes.data() + 20);
  return std::pair{width, height};
}

[[nodiscard]] std::expected<std::pair<std::uint32_t, std::uint32_t>,
                            diagnostics::Error>
probe_webp(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 16 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WEBP", 4) != 0) {
    return std::unexpected(
        make_invalid("bitmap.webp.signature", "WebP RIFF signature mismatch"));
  }

  // Prefer libwebp's header parser (handles VP8/VP8L/VP8X) without pixels.
  int width = 0;
  int height = 0;
  if (WebPGetInfo(bytes.data(), bytes.size(), &width, &height) != 1) {
    return std::unexpected(
        make_invalid("bitmap.webp.header", "WebP header probe failed"));
  }
  if (width <= 0 || height <= 0) {
    return std::unexpected(make_invalid("bitmap.webp.dimensions",
                                        "WebP reported non-positive size"));
  }
  return std::pair{static_cast<std::uint32_t>(width),
                   static_cast<std::uint32_t>(height)};
}

void premultiply_inplace(std::vector<std::uint8_t> &rgba) {
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    const std::uint32_t a = rgba[i + 3];
    rgba[i + 0] = static_cast<std::uint8_t>((rgba[i + 0] * a + 127) / 255);
    rgba[i + 1] = static_cast<std::uint8_t>((rgba[i + 1] * a + 127) / 255);
    rgba[i + 2] = static_cast<std::uint8_t>((rgba[i + 2] * a + 127) / 255);
  }
}

[[nodiscard]] std::expected<DecodedBitmap, diagnostics::Error>
decode_png(std::span<const std::uint8_t> bytes,
           const BitmapDecodeOptions &options) {
  auto dims = probe_png(bytes);
  if (!dims) {
    return std::unexpected(dims.error());
  }
  if (auto ok = check_dimensions(dims->first, dims->second, options.limits);
      !ok) {
    return std::unexpected(ok.error());
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  // Request 4 channels; stb expands gray/RGB and synthesizes opaque alpha.
  unsigned char *pixels =
      stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                            &width, &height, &channels, 4);
  if (pixels == nullptr) {
    const char *reason = stbi_failure_reason();
    return std::unexpected(make_invalid(
        "bitmap.png.decode", reason == nullptr ? "PNG decode failed" : reason));
  }

  DecodedBitmap out;
  out.format = BitmapFormat::png;
  out.width_px = static_cast<std::uint32_t>(width);
  out.height_px = static_cast<std::uint32_t>(height);
  out.alpha_mode = options.alpha_mode;
  out.color_policy = options.color_policy;
  // Re-validate after decode in case the payload lied about IHDR.
  if (auto ok = check_dimensions(out.width_px, out.height_px, options.limits);
      !ok) {
    stbi_image_free(pixels);
    return std::unexpected(ok.error());
  }
  const std::size_t bytes_needed = static_cast<std::size_t>(out.width_px) *
                                   static_cast<std::size_t>(out.height_px) * 4U;
  out.rgba.resize(bytes_needed);
  std::memcpy(out.rgba.data(), pixels, bytes_needed);
  stbi_image_free(pixels);

  if (options.alpha_mode == AlphaMode::premultiplied) {
    premultiply_inplace(out.rgba);
  }
  // color_policy assume_srgb_ignore_icc: no ICC application; samples as sRGB.
  return out;
}

[[nodiscard]] std::expected<DecodedBitmap, diagnostics::Error>
decode_webp(std::span<const std::uint8_t> bytes,
            const BitmapDecodeOptions &options) {
  auto dims = probe_webp(bytes);
  if (!dims) {
    return std::unexpected(dims.error());
  }
  if (auto ok = check_dimensions(dims->first, dims->second, options.limits);
      !ok) {
    return std::unexpected(ok.error());
  }

  WebPBitstreamFeatures features{};
  if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
    return std::unexpected(
        make_invalid("bitmap.webp.features", "WebP feature probe failed"));
  }
  if (auto ok = check_dimensions(static_cast<std::uint32_t>(features.width),
                                 static_cast<std::uint32_t>(features.height),
                                 options.limits);
      !ok) {
    return std::unexpected(ok.error());
  }

  const std::size_t bytes_needed = static_cast<std::size_t>(features.width) *
                                   static_cast<std::size_t>(features.height) *
                                   4U;
  DecodedBitmap out;
  out.format = BitmapFormat::webp;
  out.width_px = static_cast<std::uint32_t>(features.width);
  out.height_px = static_cast<std::uint32_t>(features.height);
  out.alpha_mode = options.alpha_mode;
  out.color_policy = options.color_policy;
  out.rgba.resize(bytes_needed);

  const std::uint8_t *decoded =
      WebPDecodeRGBAInto(bytes.data(), bytes.size(), out.rgba.data(),
                         out.rgba.size(), static_cast<int>(out.width_px * 4U));
  if (decoded == nullptr) {
    return std::unexpected(
        make_invalid("bitmap.webp.decode", "WebP decode failed"));
  }

  if (options.alpha_mode == AlphaMode::premultiplied) {
    premultiply_inplace(out.rgba);
  }
  return out;
}

} // namespace

std::optional<BitmapFormat>
sniff_bitmap_format(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() >= 8) {
    static constexpr std::uint8_t kPng[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (std::memcmp(bytes.data(), kPng, 8) == 0) {
      return BitmapFormat::png;
    }
  }
  if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
      std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
    return BitmapFormat::webp;
  }
  return std::nullopt;
}

std::expected<std::pair<std::uint32_t, std::uint32_t>, diagnostics::Error>
probe_bitmap_dimensions(std::span<const std::uint8_t> bytes,
                        const BitmapDecodeLimits &limits) {
  if (bytes.size() > limits.max_input_bytes) {
    return std::unexpected(make_budget("bitmap.input.limit",
                                       "compressed bitmap exceeds max bytes"));
  }
  const auto format = sniff_bitmap_format(bytes);
  if (!format) {
    return std::unexpected(
        make_invalid("bitmap.format.unknown", "not a PNG or WebP bitstream"));
  }
  auto dims =
      (*format == BitmapFormat::png) ? probe_png(bytes) : probe_webp(bytes);
  if (!dims) {
    return std::unexpected(dims.error());
  }
  if (auto ok = check_dimensions(dims->first, dims->second, limits); !ok) {
    return std::unexpected(ok.error());
  }
  return dims;
}

std::expected<DecodedBitmap, diagnostics::Error>
decode_bitmap(std::span<const std::uint8_t> bytes,
              const BitmapDecodeOptions &options) {
  if (bytes.size() > options.limits.max_input_bytes) {
    return std::unexpected(make_budget("bitmap.input.limit",
                                       "compressed bitmap exceeds max bytes"));
  }
  if (bytes.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::unexpected(make_budget(
        "bitmap.input.limit", "compressed bitmap exceeds decoder int range"));
  }
  // Explicit policy gate: only the documented sRGB/ignore-ICC path is
  // supported.
  if (options.color_policy != ColorImportPolicy::assume_srgb_ignore_icc) {
    return std::unexpected(make_invalid("bitmap.color.unsupported",
                                        "unsupported color import policy"));
  }

  const auto format = sniff_bitmap_format(bytes);
  if (!format) {
    return std::unexpected(
        make_invalid("bitmap.format.unknown", "not a PNG or WebP bitstream"));
  }
  if (*format == BitmapFormat::png) {
    return decode_png(bytes, options);
  }
  return decode_webp(bytes, options);
}

} // namespace instantee::asset_pipeline
