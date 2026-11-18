#include <instantee/typography/text_shape.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

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

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::typography;

  if (argc != 2) {
    std::cerr << "usage: notebook_demo_typography_test <repo-root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto display = root / "assets/notebook/fonts/NotoSans-Regular.ttf";
  const auto math = root / "assets/notebook/fonts/DejaVuSans.ttf";
  expect(std::filesystem::exists(display), "notebook display font offline");
  expect(std::filesystem::exists(math), "notebook math font offline");

  const std::filesystem::path stack_paths[] = {display, math};
  auto fonts = FontStack::open(stack_paths);
  expect(fonts.has_value(), "open notebook font stack");
  if (!fonts) {
    std::cerr << fonts.error().message << '\n';
    return EXIT_FAILURE;
  }

  TextShaper shaper;

  // Stable Czech body layout from pinned notebook fonts (offline).
  constexpr std::string_view kBody =
      "Odvození: a² + b² = c² pro pravoúhlý trojúhelník";
  auto body = shaper.shape(*fonts, kBody, 28.0f);
  expect(body.has_value(), "shape notebook body");
  if (body) {
    expect(!body->glyphs.empty(), "body glyphs");
    expect(body->total_x_advance_26_6 > 0, "body advance");
    auto again = shaper.shape(*fonts, kBody, 28.0f);
    expect(again.has_value(), "reshape body");
    if (again) {
      expect(again->glyphs.size() == body->glyphs.size(), "stable glyph count");
      expect(again->total_x_advance_26_6 == body->total_x_advance_26_6,
             "stable advance");
      bool same = true;
      for (std::size_t i = 0; i < body->glyphs.size(); ++i) {
        if (body->glyphs[i].glyph_id != again->glyphs[i].glyph_id ||
            body->glyphs[i].x_advance_26_6 != again->glyphs[i].x_advance_26_6) {
          same = false;
          break;
        }
      }
      expect(same, "stable glyph ids/advances");
    }
  }

  // Math/operator line prefers DejaVu coverage through the fallback stack.
  constexpr std::string_view kMath = "x = (-b ± √(b² - 4ac)) / (2a)";
  auto eq = shaper.shape(*fonts, kMath, 32.0f);
  expect(eq.has_value(), "shape math line");
  if (eq) {
    expect(!eq->glyphs.empty(), "math glyphs");
    expect(eq->total_x_advance_26_6 > 0, "math advance");
    auto again = shaper.shape(*fonts, kMath, 32.0f);
    expect(again.has_value() &&
               again->total_x_advance_26_6 == eq->total_x_advance_26_6,
           "stable math layout");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "notebook_demo_typography_test ok\n";
  return EXIT_SUCCESS;
}
