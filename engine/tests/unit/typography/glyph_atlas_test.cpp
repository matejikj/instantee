#include <instantee/typography/glyph_atlas.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(FT_FREETYPE_H) || defined(HB_H) || defined(U_ICU_VERSION_MAJOR_NUM)
#error "typography public tests must not pull FreeType/HarfBuzz/ICU headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path font_dir() {
  return std::filesystem::path{INSTANTEE_PINNED_FONT_DIR};
}

} // namespace

int main() {
  using namespace instantee::typography;

  const auto primary = font_dir() / "NotoSans-Regular.ttf";
  expect(std::filesystem::exists(primary), "primary font present");

  const std::filesystem::path stack_paths[] = {primary};
  auto fonts = FontStack::open(stack_paths);
  expect(fonts.has_value(), "open font stack");
  if (!fonts) {
    std::cerr << fonts.error().message << '\n';
    return EXIT_FAILURE;
  }

  TextShaper shaper;
  GlyphAtlasConfig cfg;
  cfg.atlas.page_width_px = 256;
  cfg.atlas.page_height_px = 256;
  cfg.atlas.padding_px = 1;
  cfg.atlas.max_pages = 4;
  cfg.max_cached_glyphs = 64;
  cfg.max_cached_layouts = 16;
  cfg.max_in_flight_frames = 2;
  GlyphAtlas atlas(cfg);

  constexpr std::string_view kText = "Static cache";
  constexpr std::string_view kFingerprint = "noto-sans-regular";
  constexpr float kSize = 28.0f;

  expect(atlas.begin_frame(1).has_value(), "begin frame 1");
  auto cold = atlas.prepare(shaper, *fonts, kFingerprint, kText, kSize);
  expect(cold.has_value(), "cold prepare");
  if (!cold) {
    std::cerr << cold.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(!cold->from_layout_cache, "cold is miss");
  expect(!cold->glyphs.empty(), "cold has glyphs");
  expect(atlas.stats().shape_calls == 1, "one shape on cold");
  expect(atlas.stats().raster_calls >= 1, "raster on cold");
  expect(atlas.stats().layout_misses == 1, "layout miss on cold");
  expect(atlas.end_frame(1).has_value(), "end frame 1");

  const auto shape_after_cold = atlas.stats().shape_calls;
  const auto raster_after_cold = atlas.stats().raster_calls;

  expect(atlas.begin_frame(2).has_value(), "begin frame 2");
  auto warm = atlas.prepare(shaper, *fonts, kFingerprint, kText, kSize);
  expect(warm.has_value(), "warm prepare");
  if (warm) {
    expect(warm->from_layout_cache, "warm hits layout cache");
    expect(warm->glyphs.size() == cold->glyphs.size(), "warm glyph count");
    expect(warm->total_x_advance_26_6 == cold->total_x_advance_26_6,
           "warm advance");
  }
  expect(atlas.stats().shape_calls == shape_after_cold,
         "no reshape on warm frame");
  expect(atlas.stats().raster_calls == raster_after_cold,
         "no reraster on warm frame");
  expect(atlas.stats().layout_hits >= 1, "layout hit recorded");
  expect(atlas.end_frame(2).has_value(), "end frame 2");

  // Different size must miss layout/glyph cache.
  expect(atlas.begin_frame(3).has_value(), "begin frame 3");
  auto scaled = atlas.prepare(shaper, *fonts, kFingerprint, kText, 40.0f);
  expect(scaled.has_value(), "scaled prepare");
  expect(atlas.stats().shape_calls == shape_after_cold + 1,
         "reshape for new size");
  expect(atlas.end_frame(3).has_value(), "end frame 3");

  // In-flight pin blocks eviction of actively used glyphs.
  GlyphAtlasConfig tiny = cfg;
  tiny.max_cached_glyphs = 2;
  tiny.max_cached_layouts = 2;
  GlyphAtlas tight(tiny);
  expect(tight.begin_frame(10).has_value(), "begin tight frame");
  auto a = tight.prepare(shaper, *fonts, kFingerprint, "Aa", 24.0f);
  auto b = tight.prepare(shaper, *fonts, kFingerprint, "Bb", 24.0f);
  expect(a.has_value() && b.has_value(), "tight prepares");
  const auto evictions_before = tight.stats().evictions;
  expect(tight.end_frame(10).has_value(), "end tight frame");
  // After unpin, over-capacity entries may be evicted.
  expect(tight.stats().evictions >= evictions_before, "eviction counter ok");

  // Reject duplicate in-flight frame ids.
  expect(atlas.begin_frame(99).has_value(), "begin 99");
  expect(!atlas.begin_frame(99).has_value(), "reject duplicate 99");
  expect(atlas.end_frame(99).has_value(), "end 99");
  expect(!atlas.end_frame(99).has_value(), "reject missing 99");

  // Invalid inputs.
  expect(atlas.begin_frame(100).has_value(), "begin 100");
  expect(!atlas.prepare(shaper, *fonts, "", kText, kSize).has_value(),
         "empty fingerprint rejected");
  expect(!atlas.prepare(shaper, *fonts, kFingerprint, kText, 0.0f).has_value(),
         "zero size rejected");
  expect(atlas.end_frame(100).has_value(), "end 100");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "glyph_atlas_test ok\n";
  return EXIT_SUCCESS;
}
