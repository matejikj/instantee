#include <instantee/math/layout.hpp>
#include <instantee/math/parse.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using instantee::math::format_math_layout_golden;
using instantee::math::layout_math;
using instantee::math::MathLayoutStyle;
using instantee::math::parse_math;
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

[[nodiscard]] bool write_if_missing(const std::filesystem::path &path,
                                    std::string_view contents) {
  if (std::filesystem::exists(path)) {
    return true;
  }
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out << contents;
  return static_cast<bool>(out);
}

[[nodiscard]] bool check_golden(const std::filesystem::path &path,
                                std::string_view actual, bool write_goldens) {
  if (write_goldens) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out << actual;
    return true;
  }
  if (!write_if_missing(path, actual)) {
    return false;
  }
  return read_file(path) == actual;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: math_layout_test <source_root> [--write-goldens]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  bool write_goldens = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--write-goldens") {
      write_goldens = true;
    }
  }
  const auto golden_dir = root / "contracts/fixtures/math-layout/goldens";

  UnitMathMetrics metrics;
  MathLayoutStyle style;
  style.size_px = 10.0f;

  struct Case {
    const char *id;
    const char *source;
  };
  const Case cases[] = {
      {"frac-ab", "\\frac{a}{b}"},
      {"sqrt-body", "\\sqrt{x}"},
      {"sqrt-index", "\\sqrt[3]{8}"},
      {"script-sup-sub", "x^{2}_{i}"},
      {"frac-nested-script", "\\frac{a^{2}}{\\sqrt{b}}"},
  };

  for (const Case &c : cases) {
    auto ast = parse_math(c.source);
    expect(ast.has_value(), c.id);
    if (!ast) {
      continue;
    }
    auto box = layout_math(*ast, metrics, style);
    expect(box.has_value(), c.id);
    if (!box) {
      std::cerr << "  layout error: " << box.error().code << '\n';
      continue;
    }

    // Geometric invariants independent of golden text.
    if (std::string_view(c.id) == "frac-ab") {
      expect(box->tag == "frac" || !box->children.empty(), "frac root");
      // Prefer finding frac child if root is a row.
      const auto *frac = &*box;
      if (box->tag == "row" && !box->children.empty()) {
        frac = &box->children.front();
      }
      expect(frac->tag == "frac", "frac tag");
      expect(frac->children.size() == 3, "num/rule/den");
      expect(frac->height_px > frac->depth_px * 0.5, "frac has height");
      expect(frac->width_px >= 5.0, "frac width covers atoms");
      bool saw_rule = false;
      for (const auto &ch : frac->children) {
        if (ch.tag == "rule") {
          saw_rule = true;
          expect(ch.height_px + ch.depth_px > 0.0, "rule thickness");
        }
        if (ch.tag == "num") {
          expect(ch.shift_px > 0.0, "numerator above axis");
        }
        if (ch.tag == "den") {
          expect(ch.shift_px < 0.0, "denominator below axis");
        }
      }
      expect(saw_rule, "frac rule present");
    }
    if (std::string_view(c.id) == "sqrt-body" ||
        std::string_view(c.id) == "sqrt-index") {
      const auto *sqrt = &*box;
      if (box->tag == "row" && !box->children.empty()) {
        sqrt = &box->children.front();
      }
      expect(sqrt->tag == "sqrt", "sqrt tag");
      bool saw_rad = false;
      bool saw_index = false;
      for (const auto &ch : sqrt->children) {
        if (ch.tag == "radicand") {
          saw_rad = true;
        }
        if (ch.tag == "index") {
          saw_index = true;
          expect(ch.shift_px > 0.0, "index raised");
        }
      }
      expect(saw_rad, "radicand");
      if (std::string_view(c.id) == "sqrt-index") {
        expect(saw_index, "index present");
      }
    }
    if (std::string_view(c.id) == "script-sup-sub") {
      const auto *script = &*box;
      if (box->tag == "row" && !box->children.empty()) {
        script = &box->children.front();
      }
      expect(script->tag == "script", "script tag");
      bool sub = false;
      bool sup = false;
      for (const auto &ch : script->children) {
        if (ch.tag == "sub") {
          sub = true;
          expect(ch.shift_px < 0.0, "sub below");
          expect(ch.width_px < script->children.front().width_px * 2.0,
                 "sub not huge");
        }
        if (ch.tag == "sup") {
          sup = true;
          expect(ch.shift_px > 0.0, "sup above");
        }
      }
      expect(sub && sup, "both scripts");
    }

    const std::string dump = format_math_layout_golden(*box);
    const auto path = golden_dir / (std::string(c.id) + ".txt");
    expect(check_golden(path, dump, write_goldens), c.id);
  }

  // Error path: invalid style.
  {
    MathLayoutStyle bad = style;
    bad.size_px = 0.0f;
    auto ast = parse_math("a");
    expect(ast.has_value(), "atom parse");
    if (ast) {
      auto box = layout_math(*ast, metrics, bad);
      expect(!box.has_value() &&
                 box.error().code == "math.layout.invalid_style",
             "reject invalid size");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
