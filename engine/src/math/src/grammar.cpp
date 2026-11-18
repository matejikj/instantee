#include <instantee/math/grammar.hpp>

#include <algorithm>
#include <string>

namespace instantee::math {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code, "math",
                            message};
}

diagnostics::Error make_budget(std::string_view code,
                               std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "math", message};
}

// Lexicographic by name — required for binary search.
constexpr MathCommandDesc kSupported[] = {
    {"!", MathCommandKind::spacing, 0},
    {"#", MathCommandKind::literal_escape, 0},
    {"$", MathCommandKind::literal_escape, 0},
    {"%", MathCommandKind::literal_escape, 0},
    {"&", MathCommandKind::literal_escape, 0},
    {",", MathCommandKind::spacing, 0},
    {":", MathCommandKind::spacing, 0},
    {";", MathCommandKind::spacing, 0},
    {"Alpha", MathCommandKind::greek, 0},
    {"Beta", MathCommandKind::greek, 0},
    {"Delta", MathCommandKind::greek, 0},
    {"Gamma", MathCommandKind::greek, 0},
    {"Lambda", MathCommandKind::greek, 0},
    {"Omega", MathCommandKind::greek, 0},
    {"Phi", MathCommandKind::greek, 0},
    {"Pi", MathCommandKind::greek, 0},
    {"Rightarrow", MathCommandKind::operator_symbol, 0},
    {"Sigma", MathCommandKind::greek, 0},
    {"Theta", MathCommandKind::greek, 0},
    {"_", MathCommandKind::literal_escape, 0},
    {"alpha", MathCommandKind::greek, 0},
    {"approx", MathCommandKind::operator_symbol, 0},
    {"arccos", MathCommandKind::function, 0},
    {"arcsin", MathCommandKind::function, 0},
    {"arctan", MathCommandKind::function, 0},
    {"bar", MathCommandKind::accent, 1},
    {"beta", MathCommandKind::greek, 0},
    {"cdot", MathCommandKind::operator_symbol, 0},
    {"cdots", MathCommandKind::operator_symbol, 0},
    {"cos", MathCommandKind::function, 0},
    {"cot", MathCommandKind::function, 0},
    {"csc", MathCommandKind::function, 0},
    {"ddot", MathCommandKind::accent, 1},
    {"delta", MathCommandKind::greek, 0},
    {"det", MathCommandKind::function, 0},
    {"div", MathCommandKind::operator_symbol, 0},
    {"dot", MathCommandKind::accent, 1},
    {"epsilon", MathCommandKind::greek, 0},
    {"equiv", MathCommandKind::operator_symbol, 0},
    {"exp", MathCommandKind::function, 0},
    {"frac", MathCommandKind::structure, 2},
    {"gamma", MathCommandKind::greek, 0},
    {"geq", MathCommandKind::operator_symbol, 0},
    {"hat", MathCommandKind::accent, 1},
    {"infty", MathCommandKind::operator_symbol, 0},
    {"int", MathCommandKind::operator_symbol, 0},
    {"lambda", MathCommandKind::greek, 0},
    {"lbrace", MathCommandKind::delimiter, 0},
    {"lceil", MathCommandKind::delimiter, 0},
    {"ldots", MathCommandKind::operator_symbol, 0},
    {"left", MathCommandKind::delimiter, 0},
    {"leftarrow", MathCommandKind::operator_symbol, 0},
    {"leq", MathCommandKind::operator_symbol, 0},
    {"lfloor", MathCommandKind::delimiter, 0},
    {"lim", MathCommandKind::function, 0},
    {"ln", MathCommandKind::function, 0},
    {"log", MathCommandKind::function, 0},
    {"mathrm", MathCommandKind::style, 1},
    {"max", MathCommandKind::function, 0},
    {"min", MathCommandKind::function, 0},
    {"mp", MathCommandKind::operator_symbol, 0},
    {"mu", MathCommandKind::greek, 0},
    {"nabla", MathCommandKind::operator_symbol, 0},
    {"neq", MathCommandKind::operator_symbol, 0},
    {"nu", MathCommandKind::greek, 0},
    {"omega", MathCommandKind::greek, 0},
    {"partial", MathCommandKind::operator_symbol, 0},
    {"phi", MathCommandKind::greek, 0},
    {"pi", MathCommandKind::greek, 0},
    {"pm", MathCommandKind::operator_symbol, 0},
    {"prod", MathCommandKind::operator_symbol, 0},
    {"qquad", MathCommandKind::spacing, 0},
    {"quad", MathCommandKind::spacing, 0},
    {"rbrace", MathCommandKind::delimiter, 0},
    {"rceil", MathCommandKind::delimiter, 0},
    {"rfloor", MathCommandKind::delimiter, 0},
    {"rho", MathCommandKind::greek, 0},
    {"right", MathCommandKind::delimiter, 0},
    {"rightarrow", MathCommandKind::operator_symbol, 0},
    {"sec", MathCommandKind::function, 0},
    {"sigma", MathCommandKind::greek, 0},
    {"sim", MathCommandKind::operator_symbol, 0},
    {"sin", MathCommandKind::function, 0},
    {"sqrt", MathCommandKind::structure, 1},
    {"sum", MathCommandKind::operator_symbol, 0},
    {"tan", MathCommandKind::function, 0},
    {"tau", MathCommandKind::greek, 0},
    {"theta", MathCommandKind::greek, 0},
    {"times", MathCommandKind::operator_symbol, 0},
    {"to", MathCommandKind::operator_symbol, 0},
    {"varepsilon", MathCommandKind::greek, 0},
    {"vec", MathCommandKind::accent, 1},
    {"{", MathCommandKind::literal_escape, 0},
    {"|", MathCommandKind::literal_escape, 0},
    {"}", MathCommandKind::literal_escape, 0},
};

// Lexicographic — TeX IO / macros / packages never accepted.
constexpr std::string_view kForbidden[] = {
    "InputIfFileExists",
    "catcode",
    "closeout",
    "csname",
    "def",
    "documentclass",
    "documentstyle",
    "edef",
    "endcsname",
    "expandafter",
    "futurelet",
    "gdef",
    "immediate",
    "include",
    "includeonly",
    "input",
    "let",
    "loop",
    "makeatletter",
    "newcommand",
    "newenvironment",
    "openin",
    "openout",
    "pdfliteral",
    "pdfobj",
    "providecommand",
    "read",
    "renewcommand",
    "repeat",
    "special",
    "usepackage",
    "verbatim",
    "write",
    "write18",
    "xdef",
};

[[nodiscard]] bool is_letter(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

[[nodiscard]] std::string offset_path(std::size_t offset) {
  return "source@" + std::to_string(offset);
}

} // namespace

std::span<const MathCommandDesc> supported_math_commands() noexcept {
  return kSupported;
}

const MathCommandDesc *
find_supported_math_command(std::string_view name) noexcept {
  const auto begin = std::begin(kSupported);
  const auto end = std::end(kSupported);
  const auto it = std::lower_bound(
      begin, end, name, [](const MathCommandDesc &desc, std::string_view key) {
        return desc.name < key;
      });
  if (it == end || it->name != name) {
    return nullptr;
  }
  return &(*it);
}

bool is_forbidden_math_command(std::string_view name) noexcept {
  return std::binary_search(std::begin(kForbidden), std::end(kForbidden), name);
}

std::expected<void, diagnostics::Error>
validate_math_grammar(std::string_view source,
                      const MathGrammarLimits &limits) {
  if (limits.max_input_bytes == 0 || limits.max_commands == 0 ||
      limits.max_brace_depth == 0) {
    return std::unexpected(make_invalid(
        "math.limits.invalid", "Math grammar limits must be positive"));
  }
  if (source.size() > limits.max_input_bytes) {
    return std::unexpected(make_budget(
        "math.input.too_large", "Math expression exceeds max_input_bytes"));
  }

  std::uint32_t brace_depth = 0;
  std::uint32_t command_count = 0;

  for (std::size_t i = 0; i < source.size();) {
    const char ch = source[i];
    if (ch == '{') {
      if (brace_depth >= limits.max_brace_depth) {
        return std::unexpected(
            make_budget("math.brace_depth.exceeded",
                        "Math expression exceeds max_brace_depth")
                .with_json_path(offset_path(i)));
      }
      ++brace_depth;
      ++i;
      continue;
    }
    if (ch == '}') {
      if (brace_depth == 0) {
        return std::unexpected(
            make_invalid("math.brace.unbalanced",
                         "Unmatched closing brace in math expression")
                .with_json_path(offset_path(i)));
      }
      --brace_depth;
      ++i;
      continue;
    }
    if (ch != '\\') {
      ++i;
      continue;
    }

    if (i + 1 >= source.size()) {
      return std::unexpected(
          make_invalid("math.command.incomplete",
                       "Trailing backslash in math expression")
              .with_json_path(offset_path(i)));
    }

    const std::size_t cmd_offset = i;
    ++i; // skip '\'
    std::string_view name;
    if (is_letter(source[i])) {
      const std::size_t start = i;
      while (i < source.size() && is_letter(source[i])) {
        ++i;
      }
      name = source.substr(start, i - start);
    } else {
      name = source.substr(i, 1);
      ++i;
    }

    if (command_count >= limits.max_commands) {
      return std::unexpected(make_budget("math.commands.exceeded",
                                         "Math expression exceeds max_commands")
                                 .with_json_path(offset_path(cmd_offset)));
    }
    ++command_count;

    if (name == "begin" || name == "end") {
      return std::unexpected(
          make_invalid("math.environment.unsupported",
                       "TeX environments are unsupported in math subset V1")
              .with_json_path(offset_path(cmd_offset)));
    }

    if (is_forbidden_math_command(name)) {
      return std::unexpected(
          make_invalid("math.command.forbidden",
                       "Forbidden TeX-like command \\" + std::string(name) +
                           "; external TeX and macros are not executed")
              .with_json_path(offset_path(cmd_offset)));
    }

    if (find_supported_math_command(name) == nullptr) {
      return std::unexpected(
          make_invalid("math.command.unsupported",
                       "Unsupported math command \\" + std::string(name))
              .with_json_path(offset_path(cmd_offset)));
    }
  }

  if (brace_depth != 0) {
    return std::unexpected(make_invalid("math.brace.unbalanced",
                                        "Unclosed brace in math expression"));
  }
  return {};
}

} // namespace instantee::math
