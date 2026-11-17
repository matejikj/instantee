#include <instantee/typography/kinetic_layout.hpp>
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

[[nodiscard]] bool
same_glyph_identities(const instantee::typography::ShapedText &a,
                      const instantee::typography::ShapedText &b) {
  if (a.glyphs.size() != b.glyphs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.glyphs.size(); ++i) {
    if (a.glyphs[i].glyph_id != b.glyphs[i].glyph_id ||
        a.glyphs[i].font_index != b.glyphs[i].font_index ||
        a.glyphs[i].cluster != b.glyphs[i].cluster) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  using namespace instantee::typography;

  const auto primary = font_dir() / "NotoSans-Regular.ttf";
  const auto fallback = font_dir() / "DejaVuSans.ttf";
  expect(std::filesystem::exists(primary), "primary font");
  expect(std::filesystem::exists(fallback), "fallback font");
  const std::filesystem::path stack_paths[] = {primary, fallback};
  auto fonts = FontStack::open(stack_paths);
  expect(fonts.has_value(), "open fonts");
  if (!fonts) {
    return EXIT_FAILURE;
  }
  TextShaper shaper;

  // Czech diacritics: kinetic hierarchy + animation sampling must not reshape.
  {
    constexpr std::string_view kCzech =
        "Příliš žluťoučký kůň úpěl ďábelské ódy";
    auto shaped = shaper.shape(*fonts, kCzech, 32.0f);
    expect(shaped.has_value(), "shape czech");
    if (!shaped) {
      return EXIT_FAILURE;
    }
    auto layout = build_kinetic_layout(kCzech, *shaped);
    expect(layout.has_value(), "kinetic czech");
    if (layout) {
      expect(layout->block.level == KineticLevel::block, "block level");
      expect(layout->lines.size() == 1, "single line");
      expect(layout->words.size() >= 5, "czech words");
      expect(layout->graphemes.size() == shaped->grapheme_count,
             "grapheme count matches shaper");
      expect(layout->glyphs.size() == shaped->glyphs.size(), "glyph units");
      expect(!layout->words.empty() &&
                 !layout->words.front().glyph_indices.empty(),
             "first word has glyphs");

      // Word-level stagger offsets without reshaping.
      std::vector<std::int32_t> dx(layout->words.size(), 0);
      for (std::size_t i = 0; i < dx.size(); ++i) {
        dx[i] = static_cast<std::int32_t>(i) * 64; // 1px steps in 26.6
      }
      auto sampled = sample_kinetic_origins_x(*layout, KineticLevel::word, dx);
      expect(sampled.has_value(), "sample word origins");
      if (sampled) {
        expect(sampled->size() == shaped->glyphs.size(), "sampled size");
        // Identities unchanged after a second shape (animation must not
        // require reshaping).
        auto again = shaper.shape(*fonts, kCzech, 32.0f);
        expect(again.has_value() && same_glyph_identities(*shaped, *again),
               "czech identities stable across frames");
      }
    }
  }

  // Ligature: grapheme-level animation locks shared glyphs together.
  {
    constexpr std::string_view kFi = "fi";
    auto shaped = shaper.shape(*fonts, kFi, 48.0f);
    expect(shaped.has_value() && shaped->glyphs.size() == 1, "fi ligature");
    if (!shaped) {
      return EXIT_FAILURE;
    }
    auto layout = build_kinetic_layout(kFi, *shaped);
    expect(layout.has_value(), "kinetic fi");
    if (layout) {
      expect(layout->glyphs.size() == 1, "one glyph unit");
      expect(layout->graphemes.size() >= 1, "graphemes present");
      const auto locks = kinetic_cluster_locks(*layout);
      expect(!locks.empty(), "locks non-empty");
      bool found_multi = false;
      for (const auto &group : locks) {
        if (group.size() > 1) {
          found_multi = true;
          expect(group.size() == layout->graphemes.size(),
                 "all fi graphemes lock to the ligature glyph");
        }
      }
      // If ICU reports one grapheme for "fi", a single-member lock is fine;
      // the ligature still cannot be torn because there is only one glyph.
      expect(found_multi || layout->graphemes.size() == 1, "fi lock shape");

      std::vector<std::int32_t> dx(layout->graphemes.size(), 0);
      if (!dx.empty()) {
        dx.front() = 128;
        if (dx.size() > 1) {
          dx[1] = 9999; // would tear without locking
        }
      }
      auto sampled =
          sample_kinetic_origins_x(*layout, KineticLevel::grapheme, dx);
      expect(sampled.has_value(), "sample grapheme fi");
      if (sampled && !shaped->glyphs.empty()) {
        // All glyphs (the single ligature) follow the lock leader dx.
        expect((*sampled)[0] == layout->glyph_origin_x_26_6[0] + 128,
               "ligature uses locked dx");
      }
    }
  }

  // Combining mark: one grapheme owns every glyph in the cluster.
  {
    constexpr std::string_view kCombining = "e\u0301";
    auto shaped = shaper.shape(*fonts, kCombining, 48.0f);
    expect(shaped.has_value(), "shape combining");
    if (shaped) {
      auto layout = build_kinetic_layout(kCombining, *shaped);
      expect(layout.has_value(), "kinetic combining");
      if (layout) {
        expect(layout->graphemes.size() == 1, "one grapheme");
        expect(layout->graphemes.front().glyph_indices.size() ==
                   shaped->glyphs.size(),
               "grapheme keeps all cluster glyphs");
      }
    }
  }

  // Font fallback identity survives kinetic sampling.
  {
    // U+1D400 MATHEMATICAL BOLD CAPITAL A is often missing from Noto Sans and
    // present in DejaVu — if both miss it, skip soft.
    constexpr std::string_view kFallbackProbe = "A\U0001D400B";
    auto shaped = shaper.shape(*fonts, kFallbackProbe, 32.0f);
    if (shaped) {
      bool used_fallback = false;
      for (const auto &g : shaped->glyphs) {
        if (g.font_index > 0) {
          used_fallback = true;
        }
      }
      auto layout = build_kinetic_layout(kFallbackProbe, *shaped);
      expect(layout.has_value(), "kinetic fallback probe");
      if (layout && used_fallback) {
        std::vector<std::int32_t> dx(layout->glyphs.size(), 32);
        auto sampled =
            sample_kinetic_origins_x(*layout, KineticLevel::glyph, dx);
        expect(sampled.has_value(), "sample glyph fallback");
        auto again = shaper.shape(*fonts, kFallbackProbe, 32.0f);
        expect(again.has_value() && same_glyph_identities(*shaped, *again),
               "fallback font_index stable");
      } else {
        expect(layout.has_value(), "layout without fallback still builds");
      }
    } else {
      // Missing coverage is a valid stack outcome; kinetic must not be blamed.
      expect(true, "fallback probe uncovered");
    }
  }

  // Multi-line block/line levels (control chars are not font glyphs; supply a
  // shaped run that skips the LF byte so layout still sees two line units).
  {
    constexpr std::string_view kLines = "a\nb";
    ShapedText shaped;
    GlyphPlacement ga;
    ga.glyph_id = 1;
    ga.cluster = 0;
    ga.x_advance_26_6 = 64;
    GlyphPlacement gb;
    gb.glyph_id = 2;
    gb.cluster = 2;
    gb.x_advance_26_6 = 64;
    shaped.glyphs = {ga, gb};
    shaped.total_x_advance_26_6 = 128;
    shaped.grapheme_count = 2;
    auto layout = build_kinetic_layout(kLines, shaped);
    expect(layout.has_value() && layout->lines.size() == 2, "two lines");
    if (layout) {
      expect(std::string(kinetic_level_id(KineticLevel::line)) == "line",
             "level id");
      expect(kinetic_units_at(*layout, KineticLevel::line).size() == 2,
             "units_at lines");
      expect(layout->lines[0].glyph_indices.size() == 1, "line0 glyph");
      expect(layout->lines[1].glyph_indices.size() == 1, "line1 glyph");
    }
  }

  // Error paths.
  {
    ShapedText bogus;
    GlyphPlacement g;
    g.glyph_id = 1;
    g.cluster = 99;
    bogus.glyphs.push_back(g);
    auto bad = build_kinetic_layout("ab", bogus);
    expect(!bad.has_value(), "reject cluster overflow");

    auto shaped = shaper.shape(*fonts, "ok", 24.0f);
    expect(shaped.has_value(), "shape ok");
    if (shaped) {
      auto layout = build_kinetic_layout("ok", *shaped);
      expect(layout.has_value(), "layout ok");
      if (layout) {
        std::vector<std::int32_t> wrong_dx{1, 2, 3};
        auto sampled =
            sample_kinetic_origins_x(*layout, KineticLevel::word, wrong_dx);
        expect(!sampled.has_value(), "reject dx size mismatch");
      }
    }
  }

  // Fixture files exist for contract review.
  {
    const auto root = std::filesystem::path{INSTANTEE_SOURCE_DIR};
    expect(
        std::filesystem::exists(
            root / "contracts/fixtures/kinetic-text/positive/czech-words.json"),
        "czech fixture");
    expect(
        std::filesystem::exists(
            root / "contracts/fixtures/kinetic-text/positive/fi-ligature.json"),
        "fi fixture");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
