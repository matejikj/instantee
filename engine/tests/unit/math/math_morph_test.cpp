#include <instantee/math/layout.hpp>
#include <instantee/math/morph.hpp>
#include <instantee/math/parse.hpp>
#include <instantee/math/semantic.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::math::compile_math_anim;
using instantee::math::format_math_morph_golden;
using instantee::math::layout_math;
using instantee::math::MathLayoutStyle;
using instantee::math::MathMorphEase;
using instantee::math::MathMorphRole;
using instantee::math::MathMorphToken;
using instantee::math::parse_math;
using instantee::math::plan_math_morph;
using instantee::math::sample_math_morph;
using instantee::math::sample_math_morph_ease;
using instantee::math::UnitMathMetrics;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path);
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens || !std::filesystem::exists(path)) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    if (write_goldens) {
      return true;
    }
  }
  return read_file(path) == actual;
}

[[nodiscard]] std::optional<instantee::math::MathAnimScene>
compile_expr(std::string_view src, UnitMathMetrics &metrics,
             const MathLayoutStyle &style) {
  auto ast = parse_math(src);
  if (!ast) {
    std::cerr << "parse failed: " << src << '\n';
    return std::nullopt;
  }
  auto layout = layout_math(*ast, metrics, style);
  if (!layout) {
    std::cerr << "layout failed: " << src << '\n';
    return std::nullopt;
  }
  auto scene = compile_math_anim(*ast, *layout);
  if (!scene) {
    std::cerr << "compile failed: " << src << " (" << scene.error().code
              << ")\n";
    return std::nullopt;
  }
  return *scene;
}

[[nodiscard]] const MathMorphToken *
find_token(const std::vector<MathMorphToken> &tokens, std::string_view glyph,
           MathMorphRole role) {
  for (const auto &t : tokens) {
    if (t.glyph_text == glyph && t.role == role) {
      return &t;
    }
  }
  return nullptr;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: math_morph_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }

  expect(sample_math_morph_ease(MathMorphEase::linear, 0.0) == 0.0,
         "ease linear 0");
  expect(sample_math_morph_ease(MathMorphEase::linear, 1.0) == 1.0,
         "ease linear 1");
  expect(sample_math_morph_ease(MathMorphEase::ease_in_out, 0.0) == 0.0,
         "ease inout 0");
  expect(sample_math_morph_ease(MathMorphEase::ease_in_out, 1.0) == 1.0,
         "ease inout 1");
  expect(std::abs(sample_math_morph_ease(MathMorphEase::ease_in_out, 0.5) -
                  0.5) < 1e-12,
         "ease inout mid");

  UnitMathMetrics metrics;
  MathLayoutStyle style;
  style.size_px = 10.0f;

  auto from = compile_expr("\\frac{a}{b}", metrics, style);
  auto to = compile_expr("\\frac{a}{c}", metrics, style);
  expect(from.has_value() && to.has_value(), "compile frac morph pair");
  if (!from || !to) {
    return EXIT_FAILURE;
  }

  auto plan = plan_math_morph(*from, *to);
  expect(plan.has_value(), "plan morph");
  if (!plan) {
    return EXIT_FAILURE;
  }

  bool saw_persist_a = false;
  bool saw_exit_b = false;
  bool saw_enter_c = false;
  for (const auto &p : plan->pairs) {
    if (p.role == MathMorphRole::persist &&
        p.from_id.find("atom:a") != std::string::npos) {
      saw_persist_a = true;
    }
    if (p.role == MathMorphRole::exit &&
        p.from_id.find("atom:b") != std::string::npos) {
      saw_exit_b = true;
    }
    if (p.role == MathMorphRole::enter &&
        p.to_id.find("atom:c") != std::string::npos) {
      saw_enter_c = true;
    }
  }
  expect(saw_persist_a, "persist numerator a");
  expect(saw_exit_b, "exit denominator b");
  expect(saw_enter_c, "enter denominator c");

  auto at0 = sample_math_morph(*plan, *from, *to, 0.0);
  auto at_mid = sample_math_morph(*plan, *from, *to, 0.5);
  auto at1 = sample_math_morph(*plan, *from, *to, 1.0);
  expect(at0 && at_mid && at1, "sample times");
  if (!at0 || !at_mid || !at1) {
    return EXIT_FAILURE;
  }

  // Endpoints: exit fully visible at start, enter fully visible at end.
  const auto *exit_b0 = find_token(*at0, "b", MathMorphRole::exit);
  const auto *enter_c0 = find_token(*at0, "c", MathMorphRole::enter);
  const auto *exit_b1 = find_token(*at1, "b", MathMorphRole::exit);
  const auto *enter_c1 = find_token(*at1, "c", MathMorphRole::enter);
  expect(exit_b0 && exit_b0->opacity == 1.0, "exit b opaque at t=0");
  expect(enter_c0 && enter_c0->opacity == 0.0, "enter c hidden at t=0");
  expect(exit_b1 && exit_b1->opacity == 0.0, "exit b gone at t=1");
  expect(enter_c1 && enter_c1->opacity == 1.0, "enter c opaque at t=1");

  // No broken tokens: glyph strings never blend.
  for (const auto &t : *at_mid) {
    expect(t.glyph_text == "a" || t.glyph_text == "b" || t.glyph_text == "c",
           "glyph stays atomic");
  }

  // Temporal stability.
  auto at_mid2 = sample_math_morph(*plan, *from, *to, 0.5);
  expect(at_mid2.has_value() && at_mid2->size() == at_mid->size(),
         "stable resample size");
  if (at_mid2) {
    for (std::size_t i = 0; i < at_mid->size(); ++i) {
      expect((*at_mid)[i].correspondence_key ==
                     (*at_mid2)[i].correspondence_key &&
                 (*at_mid)[i].glyph_text == (*at_mid2)[i].glyph_text &&
                 (*at_mid)[i].opacity == (*at_mid2)[i].opacity,
             "stable resample values");
    }
  }

  auto bad = sample_math_morph(*plan, *from, *to, std::nan(""));
  expect(!bad.has_value() && bad.error().code == "math_morph_bad_time",
         "reject non-finite time");

  const auto golden =
      format_math_morph_golden("frac-a-b-to-a-c", *plan, *at0, *at_mid, *at1);
  const auto golden_path =
      root / "contracts/fixtures/math-morph/goldens/frac-a-b-to-a-c.txt";
  expect(check_golden(golden_path, golden, write_goldens),
         "equation morph golden");

  // Structural rewrite: new radical appears as enter, old frac leaves exit.
  auto from2 = compile_expr("a+b", metrics, style);
  auto to2 = compile_expr("\\sqrt{a+b}", metrics, style);
  expect(from2 && to2, "compile radical morph pair");
  if (from2 && to2) {
    auto plan2 = plan_math_morph(*from2, *to2);
    expect(plan2.has_value(), "plan radical morph");
    if (plan2) {
      auto s0 = sample_math_morph(*plan2, *from2, *to2, 0.0);
      auto sm = sample_math_morph(*plan2, *from2, *to2, 0.5);
      auto s1 = sample_math_morph(*plan2, *from2, *to2, 1.0);
      expect(s0 && sm && s1, "sample radical morph");
      if (s0 && sm && s1) {
        const auto g2 =
            format_math_morph_golden("row-to-sqrt", *plan2, *s0, *sm, *s1);
        expect(check_golden(root / "contracts/fixtures/math-morph/goldens/"
                                   "row-to-sqrt.txt",
                            g2, write_goldens),
               "row-to-sqrt golden");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "math_morph_test: ok\n";
  return EXIT_SUCCESS;
}
