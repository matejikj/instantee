#include <instantee/math/ast.hpp>
#include <instantee/math/parse.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::math::count_math_nodes;
using instantee::math::is_empty_row;
using instantee::math::math_ast_depth;
using instantee::math::MathNode;
using instantee::math::MathNodeKind;
using instantee::math::MathParseLimits;
using instantee::math::parse_math;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// SplitMix64 — deterministic fuzz stream (same family as creative variation).
class SplitMix64 {
public:
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31U);
  }

  std::uint32_t next_u32(std::uint32_t bound) noexcept {
    if (bound == 0) {
      return 0;
    }
    return static_cast<std::uint32_t>(next() % bound);
  }

private:
  std::uint64_t state_;
};

[[nodiscard]] const MathNode *first_child(const MathNode &node) {
  return node.children.empty() ? nullptr : &node.children.front();
}

[[nodiscard]] std::string generate_valid(SplitMix64 &rng, int budget) {
  if (budget <= 1) {
    static constexpr std::string_view atoms[] = {"a", "b",       "x",      "1",
                                                 "2", "\\alpha", "\\beta", "+"};
    return std::string(atoms[rng.next_u32(8)]);
  }
  switch (rng.next_u32(6)) {
  case 0:
    return "\\frac{" + generate_valid(rng, budget - 1) + "}{" +
           generate_valid(rng, budget - 1) + "}";
  case 1:
    return "\\sqrt{" + generate_valid(rng, budget - 1) + "}";
  case 2:
    return "{" + generate_valid(rng, budget - 1) + "}";
  case 3:
    return "{" + generate_valid(rng, budget - 1) + "}^{" +
           generate_valid(rng, budget - 1) + "}";
  case 4:
    return "{" + generate_valid(rng, budget - 1) + "}_{" +
           generate_valid(rng, budget - 1) + "}";
  default:
    return generate_valid(rng, budget - 1) + "+" +
           generate_valid(rng, budget - 1);
  }
}

[[nodiscard]] bool known_math_error(std::string_view code) {
  return code.starts_with("math.");
}

} // namespace

int main() {
  // Structural fixtures: fractions, roots, scripts, symbols, delimiters.
  {
    auto tree = parse_math("\\frac{a+b}{\\sqrt{c_d}}");
    expect(tree.has_value(), "parse frac/sqrt");
    if (tree) {
      expect(tree->kind == MathNodeKind::row, "root row");
      expect(!tree->children.empty(), "root has child");
      const MathNode *frac = first_child(*tree);
      expect(frac && frac->kind == MathNodeKind::frac, "frac node");
      if (frac && frac->children.size() == 2) {
        expect(frac->children[0].kind == MathNodeKind::group, "num group");
        expect(frac->children[1].kind == MathNodeKind::group, "den group");
        const MathNode &den_row = frac->children[1].children.front();
        expect(!den_row.children.empty() &&
                   den_row.children.front().kind == MathNodeKind::sqrt,
               "den sqrt");
      }
      expect(count_math_nodes(*tree) >= 5, "node count positive");
      expect(math_ast_depth(*tree) >= 3, "depth nested");
    }
  }

  {
    auto tree = parse_math("x^{2}_{i}");
    expect(tree.has_value(), "parse scripts");
    if (tree && !tree->children.empty()) {
      const MathNode &script = tree->children.front();
      expect(script.kind == MathNodeKind::script, "script kind");
      expect(script.children.size() == 3, "script arity");
      expect(!is_empty_row(script.children[1]), "has subscript");
      expect(!is_empty_row(script.children[2]), "has superscript");
    }
  }

  {
    auto tree = parse_math("\\left(\\alpha+\\beta\\right)");
    expect(tree.has_value(), "parse left/right");
    if (tree && !tree->children.empty()) {
      expect(tree->children.front().kind == MathNodeKind::delimited,
             "delimited");
      expect(tree->children.front().value == "(,)", "delimiter pair");
    }
  }

  {
    auto tree = parse_math("\\sqrt[3]{8}");
    expect(tree.has_value(), "parse indexed sqrt");
    if (tree && !tree->children.empty()) {
      const MathNode &sqrt = tree->children.front();
      expect(sqrt.kind == MathNodeKind::sqrt && sqrt.children.size() == 2,
             "sqrt with index");
    }
  }

  {
    auto tree = parse_math("\\sin\\theta + \\mathrm{x}");
    expect(tree.has_value(), "parse symbols/style");
  }

  // Depth / range rejection.
  {
    MathParseLimits limits;
    limits.max_ast_depth = 3;
    std::string deep = "a";
    for (int i = 0; i < 8; ++i) {
      deep = "\\frac{" + deep + "}{b}";
    }
    auto bad = parse_math(deep, limits);
    expect(!bad.has_value(), "deep frac rejected");
    if (!bad) {
      expect(bad.error().code == "math.ast.depth_exceeded" ||
                 bad.error().code == "math.brace_depth.exceeded",
             "depth error code");
    }
  }

  {
    MathParseLimits limits;
    limits.max_ast_nodes = 4;
    auto bad = parse_math("\\frac{a}{b}+\\frac{c}{d}", limits);
    expect(!bad.has_value() && bad.error().code == "math.ast.nodes_exceeded",
           "node budget");
  }

  {
    MathParseLimits limits;
    limits.max_row_length = 2;
    auto bad = parse_math("a+b+c", limits);
    expect(!bad.has_value() && bad.error().code == "math.ast.row_exceeded",
           "row budget");
  }

  {
    auto bad = parse_math("\\input{x}");
    expect(!bad.has_value() && bad.error().code == "math.command.forbidden",
           "malicious input rejected before AST");
  }

  {
    auto bad = parse_math("\\unknown");
    expect(!bad.has_value() && bad.error().code == "math.command.unsupported",
           "unsupported rejected");
  }

  {
    auto bad = parse_math("a^b^c");
    expect(!bad.has_value() &&
               bad.error().code == "math.parse.duplicate_superscript",
           "duplicate superscript");
  }

  {
    MathParseLimits limits;
    limits.grammar.max_input_bytes = 8;
    auto bad = parse_math("abcdefghijk", limits);
    expect(!bad.has_value() && bad.error().code == "math.input.too_large",
           "unbounded size rejected");
  }

  // Property: randomly generated valid expressions parse under default limits.
  {
    SplitMix64 rng(0xC0FFEEULL);
    int parsed = 0;
    for (int i = 0; i < 64; ++i) {
      const std::string expr = generate_valid(rng, 4);
      auto tree = parse_math(expr);
      if (!tree) {
        std::cerr << "FAIL: generated expr rejected: " << expr << " ["
                  << tree.error().code << "]\n";
        ++failures;
        continue;
      }
      expect(count_math_nodes(*tree) >= 1, "generated has nodes");
      expect(math_ast_depth(*tree) >= 1, "generated has depth");
      expect(count_math_nodes(*tree) <= 1024, "generated within default nodes");
      ++parsed;
    }
    expect(parsed == 64, "all generated exprs parsed");
  }

  // Fuzz: arbitrary byte-ish strings never crash; failures use math.* codes.
  {
    SplitMix64 rng(0xF00DULL);
    static constexpr char alphabet[] =
        "\\abcdefghijklmnopqrstuvwxyz{}^_-+*/[]() 0123456789";
    const std::size_t alpha_n = sizeof(alphabet) - 1;
    for (int i = 0; i < 128; ++i) {
      const std::uint32_t len = 1 + rng.next_u32(48);
      std::string junk;
      junk.reserve(len);
      for (std::uint32_t k = 0; k < len; ++k) {
        junk.push_back(
            alphabet[rng.next_u32(static_cast<std::uint32_t>(alpha_n))]);
      }
      auto result = parse_math(junk);
      if (!result) {
        expect(known_math_error(result.error().code), "fuzz error namespace");
      } else {
        expect(count_math_nodes(*result) >= 1, "fuzz parse non-empty");
      }
    }
  }

  // Nested braces beyond grammar brace depth fail closed.
  {
    MathParseLimits limits;
    limits.grammar.max_brace_depth = 2;
    auto bad = parse_math("{{{a}}}", limits);
    expect(!bad.has_value(), "brace depth rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
