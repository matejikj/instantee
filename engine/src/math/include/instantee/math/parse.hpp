#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/math/ast.hpp>
#include <instantee/math/grammar.hpp>

#include <cstdint>
#include <expected>
#include <string_view>

namespace instantee::math {

struct MathParseLimits {
  MathGrammarLimits grammar{};
  // Total AST nodes including the root row.
  std::uint32_t max_ast_nodes = 1024;
  // Root depth is 1; each nested group/frac/sqrt/script/… adds one.
  std::uint32_t max_ast_depth = 32;
  std::uint32_t max_row_length = 256;
};

// Parse a V1 math expression into a typed AST. Runs the grammar pre-scan first
// (forbidden/unsupported commands, brace/command budgets), then builds nodes
// while enforcing AST depth/node/row limits. Never invokes external TeX.
[[nodiscard]] std::expected<MathNode, diagnostics::Error>
parse_math(std::string_view source, const MathParseLimits &limits = {});

} // namespace instantee::math
