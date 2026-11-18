#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace instantee::math {

// Semantic role a supported control sequence lowers into for the later AST.
enum class MathCommandKind : std::uint8_t {
  structure = 1,
  operator_symbol,
  greek,
  function,
  delimiter,
  spacing,
  accent,
  style,
  literal_escape,
};

struct MathCommandDesc {
  std::string_view name; // without leading '\'
  MathCommandKind kind = MathCommandKind::operator_symbol;
  // Braced arguments the parser expects after the command (0 = atom).
  std::uint8_t arity = 0;
};

struct MathGrammarLimits {
  std::size_t max_input_bytes = 8U * 1024U;
  std::uint32_t max_commands = 256;
  std::uint32_t max_brace_depth = 32;
};

// Sorted allowlist (lexicographic by name). Stable across calls.
[[nodiscard]] std::span<const MathCommandDesc>
supported_math_commands() noexcept;

[[nodiscard]] const MathCommandDesc *
find_supported_math_command(std::string_view name) noexcept;

// True for names that must never be accepted even if someone extends the
// allowlist carelessly (IO, macros, shell, packages).
[[nodiscard]] bool is_forbidden_math_command(std::string_view name) noexcept;

// Grammar pre-scan only: enforces size/depth budgets, rejects forbidden and
// unsupported control sequences with precise diagnostics, and never invokes
// an external TeX engine. Prefer parse_math when an AST is required.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_math_grammar(std::string_view source,
                      const MathGrammarLimits &limits = {});

} // namespace instantee::math
