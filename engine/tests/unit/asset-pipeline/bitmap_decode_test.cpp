#include <instantee/asset_pipeline/bitmap_decode.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

#if defined(STBI_VERSION) || defined(WEBP_WEBP_H_) || defined(WebPGetInfo)
#error "asset-pipeline public tests must not pull decoder headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// 2x2 RGBA PNG: R G / B W
constexpr std::uint8_t kPng2x2[] = {
    137, 80,  78,  71, 13, 10,  26, 10, 0,  0,  0,  13,  73,  72,  68,
    82,  0,   0,   0,  2,  0,   0,  0,  2,  8,  6,  0,   0,   0,   114,
    182, 13,  36,  0,  0,  0,   18, 73, 68, 65, 84, 120, 218, 99,  248,
    207, 192, 240, 31, 12, 129, 52, 24, 0,  0,  73, 200, 9,   247, 3,
    217, 100, 241, 0,  0,  0,   0,  73, 69, 78, 68, 174, 66,  96,  130};

// PNG IHDR claiming 100000x100000 (no IDAT) — must fail before allocation.
constexpr std::uint8_t kPngHugeHeader[] = {
    137, 80,  78,  71, 13, 10,  26,  10, 0, 0, 0, 13, 73, 72, 68,  82, 0,
    1,   134, 160, 0,  1,  134, 160, 8,  2, 0, 0, 0,  39, 48, 156, 159};

// 1x1 lossless WebP (palette) with RGB(10,20,30).
constexpr std::uint8_t kWebp1x1[] = {
    82, 73,  70, 70,  30,  0,   0,   0,   87,  69,  66, 80, 86,
    80, 56,  76, 17,  0,   0,   0,   47,  0,   0,   0,  0,  7,
    80, 138, 42, 212, 163, 255, 129, 136, 232, 127, 0,  0};

// VP8X canvas 100000x100000 — probe must reject on pixel budget.
constexpr std::uint8_t kWebpHugeHeader[] = {
    82, 73, 70, 70, 22, 0, 0, 0, 87, 69,  66,  80, 86,  80,  56,
    88, 10, 0,  0,  0,  0, 0, 0, 0,  159, 134, 1,  159, 134, 1};

} // namespace

int main() {
  using namespace instantee::asset_pipeline;

  expect(sniff_bitmap_format(std::span{kPng2x2}) == BitmapFormat::png,
         "sniff png");
  expect(sniff_bitmap_format(std::span{kWebp1x1}) == BitmapFormat::webp,
         "sniff webp");
  {
    constexpr std::uint8_t unknown[] = {1, 2, 3, 4};
    expect(!sniff_bitmap_format(std::span{unknown}).has_value(),
           "sniff unknown");
  }

  {
    auto png = decode_bitmap(std::span{kPng2x2});
    expect(png.has_value(), "decode png 2x2");
    if (png) {
      expect(png->width_px == 2 && png->height_px == 2, "png size");
      expect(png->rgba.size() == 16, "png rgba bytes");
      expect(png->rgba[0] == 255 && png->rgba[1] == 0 && png->rgba[2] == 0 &&
                 png->rgba[3] == 255,
             "png red");
      expect(png->color_policy == ColorImportPolicy::assume_srgb_ignore_icc,
             "png srgb policy");
      expect(png->alpha_mode == AlphaMode::straight, "png straight alpha");
    }
  }

  {
    BitmapDecodeOptions opts;
    opts.alpha_mode = AlphaMode::premultiplied;
    auto png = decode_bitmap(std::span{kPng2x2}, opts);
    expect(png.has_value() && png->alpha_mode == AlphaMode::premultiplied,
           "png premultiply mode");
    // Opaque red stays red after premultiply.
    expect(png && png->rgba[0] == 255 && png->rgba[3] == 255,
           "opaque premultiply");
  }

  {
    auto webp = decode_bitmap(std::span{kWebp1x1});
    expect(webp.has_value(), "decode webp 1x1");
    if (webp) {
      expect(webp->width_px == 1 && webp->height_px == 1, "webp size");
      expect(webp->rgba.size() == 4, "webp rgba bytes");
      expect(webp->rgba[0] == 10 && webp->rgba[1] == 20 &&
                 webp->rgba[2] == 30 && webp->rgba[3] == 255,
             "webp rgb");
    }
  }

  {
    auto bad = decode_bitmap(std::span{kPngHugeHeader});
    expect(!bad.has_value(), "huge png rejected");
    expect(!bad && (bad.error().code == "bitmap.dimensions.limit" ||
                    bad.error().code == "bitmap.pixels.limit"),
           "huge png budget code");
  }

  {
    auto bad = decode_bitmap(std::span{kWebpHugeHeader});
    expect(!bad.has_value(), "huge webp rejected");
    expect(!bad && (bad.error().code == "bitmap.dimensions.limit" ||
                    bad.error().code == "bitmap.pixels.limit" ||
                    bad.error().code == "bitmap.webp.header"),
           "huge webp error");
  }

  {
    const std::uint8_t truncated[] = {137, 80, 78, 71, 13, 10};
    auto bad = decode_bitmap(std::span{truncated});
    expect(!bad.has_value(), "truncated png rejected");
  }

  {
    const std::uint8_t junk[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    auto bad = decode_bitmap(std::span{junk});
    expect(!bad.has_value() && bad.error().code == "bitmap.format.unknown",
           "junk rejected");
  }

  {
    BitmapDecodeOptions opts;
    opts.limits.max_input_bytes = 16;
    auto bad = decode_bitmap(std::span{kPng2x2}, opts);
    expect(!bad.has_value() && bad.error().code == "bitmap.input.limit",
           "input byte budget");
  }

  {
    // Corrupt PNG: valid signature/IHDR dims but broken IDAT.
    std::vector<std::uint8_t> corrupt(kPng2x2, kPng2x2 + sizeof(kPng2x2));
    if (corrupt.size() > 40) {
      corrupt[40] ^= 0xff;
    }
    auto bad = decode_bitmap(std::span{corrupt});
    expect(!bad.has_value(), "corrupt png rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "bitmap_decode_test ok\n";
  return EXIT_SUCCESS;
}
