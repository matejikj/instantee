#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace instantee::math {

// Typed math tree produced by the V1 TeX-like parser. Layout/animation attach
// later; this structure only captures semantic shape.
enum class MathNodeKind : std::uint8_t {
  row = 1,   // ordered sequence of siblings
  group,     // `{...}`
  atom,      // single letter
  number,    // digit run
  symbol,    // named allowlisted command (greek/op/function/…)
  literal,   // punctuation or escaped literal
  frac,      // children: numerator, denominator
  sqrt,      // children: body, or index then body
  script,    // children: base, subscript, superscript (empty row = absent)
  accent,    // value = accent name; children: body
  style,     // value = style name (e.g. mathrm); children: body
  delimited, // value = open+","+close; children: body
  space,     // value = spacing command name or literal space marker
};

struct MathNode {
  MathNodeKind kind = MathNodeKind::row;
  std::string value{};
  std::vector<MathNode> children{};
};

[[nodiscard]] inline bool is_empty_row(const MathNode &node) noexcept {
  return node.kind == MathNodeKind::row && node.children.empty() &&
         node.value.empty();
}

[[nodiscard]] std::uint32_t count_math_nodes(const MathNode &root) noexcept;
[[nodiscard]] std::uint32_t math_ast_depth(const MathNode &root) noexcept;

} // namespace instantee::math
