#include <instantee/typography/text_shape.hpp>

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

[[nodiscard]] bool has_notdef(const instantee::typography::ShapedText &shaped) {
  for (const auto &g : shaped.glyphs) {
    if (g.glyph_id == 0) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  using namespace instantee::typography;

  const auto primary = font_dir() / "NotoSans-Regular.ttf";
  const auto fallback = font_dir() / "DejaVuSans.ttf";
  expect(std::filesystem::exists(primary), "primary font present");
  expect(std::filesystem::exists(fallback), "fallback font present");

  const std::filesystem::path stack_paths[] = {primary, fallback};
  auto fonts = FontStack::open(stack_paths);
  expect(fonts.has_value(), "open font stack");
  if (!fonts) {
    std::cerr << fonts.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(fonts->size() == 2, "two faces");

  TextShaper shaper;

  // Czech pangram with diacritics.
  {
    constexpr std::string_view kCzech =
        "Příliš žluťoučký kůň úpěl ďábelské ódy";
    auto shaped = shaper.shape(*fonts, kCzech, 32.0f);
    expect(shaped.has_value(), "shape czech");
    if (shaped) {
      expect(!shaped->glyphs.empty(), "czech glyphs");
      expect(!has_notdef(*shaped), "czech no notdef");
      expect(shaped->total_x_advance_26_6 > 0, "czech advance");
      expect(shaped->paragraph_level == 0, "czech LTR");
      // Diacritic letters are single graphemes; spaces count too.
      expect(shaped->grapheme_count >= 20, "czech grapheme count");
      // Reproducible: second shape matches glyph ids and advances.
      auto again = shaper.shape(*fonts, kCzech, 32.0f);
      expect(again.has_value() && again->glyphs.size() == shaped->glyphs.size(),
             "czech repro size");
      if (again && again->glyphs.size() == shaped->glyphs.size()) {
        bool same = true;
        for (std::size_t i = 0; i < shaped->glyphs.size(); ++i) {
          if (shaped->glyphs[i].glyph_id != again->glyphs[i].glyph_id ||
              shaped->glyphs[i].x_advance_26_6 !=
                  again->glyphs[i].x_advance_26_6 ||
              shaped->glyphs[i].cluster != again->glyphs[i].cluster) {
            same = false;
            break;
          }
        }
        expect(same, "czech repro placements");
      }
    }
  }

  // Combining mark: e + combining acute should be one grapheme.
  {
    constexpr std::string_view kCombining = "e\u0301"; // é
    auto shaped = shaper.shape(*fonts, kCombining, 48.0f);
    expect(shaped.has_value(), "shape combining");
    if (shaped) {
      expect(!has_notdef(*shaped), "combining no notdef");
      expect(shaped->grapheme_count == 1, "combining one grapheme");
      expect(!shaped->glyphs.empty(), "combining has glyphs");
      // Mark may be a separate positioned glyph; base cluster starts at 0.
      expect(shaped->glyphs.front().cluster == 0, "combining base cluster");
    }
  }

  // Ligature: "fi" with liga should collapse to fewer than two advances when
  // the face provides an fi ligature (Noto Sans does).
  {
    auto with_liga = shaper.shape(*fonts, "fi", 48.0f);
    expect(with_liga.has_value(), "shape fi");
    if (with_liga) {
      expect(!has_notdef(*with_liga), "fi no notdef");
      expect(with_liga->glyphs.size() == 1, "fi ligature one glyph");
      expect(with_liga->glyphs.front().cluster == 0, "fi cluster");
    }
    auto plain = shaper.shape(*fonts, "f i", 48.0f);
    expect(plain.has_value() && plain->glyphs.size() >= 2, "f space i");
  }

  // Raster smoke: first czech glyph produces a non-empty coverage bitmap.
  {
    auto shaped = shaper.shape(*fonts, "A", 64.0f);
    expect(shaped.has_value() && !shaped->glyphs.empty(), "shape A");
    if (shaped && !shaped->glyphs.empty()) {
      auto bmp = shaper.rasterize(*fonts, shaped->glyphs.front().font_index,
                                  shaped->glyphs.front().glyph_id, 64.0f);
      expect(bmp.has_value(), "raster A");
      if (bmp) {
        expect(bmp->width_px > 0 && bmp->height_px > 0, "raster size");
        expect(bmp->gray.size() ==
                   static_cast<std::size_t>(bmp->width_px) * bmp->height_px,
               "raster bytes");
        bool any = false;
        for (auto v : bmp->gray) {
          if (v != 0) {
            any = true;
            break;
          }
        }
        expect(any, "raster coverage");
      }
    }
  }

  // Empty / invalid inputs.
  {
    auto empty = shaper.shape(*fonts, "", 16.0f);
    expect(empty.has_value() && empty->glyphs.empty(), "empty text");
    auto bad_size = shaper.shape(*fonts, "x", 0.0f);
    expect(!bad_size.has_value(), "zero size rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
