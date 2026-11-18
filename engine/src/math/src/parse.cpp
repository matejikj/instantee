#include <instantee/math/parse.hpp>

#include <cctype>
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

[[nodiscard]] std::string offset_path(std::size_t offset) {
  return "source@" + std::to_string(offset);
}

[[nodiscard]] bool is_letter(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

[[nodiscard]] bool is_digit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

struct Parser {
  std::string_view source;
  MathParseLimits limits;
  std::size_t pos = 0;
  std::uint32_t nodes = 0;

  [[nodiscard]] bool at_end() const noexcept { return pos >= source.size(); }

  [[nodiscard]] char peek() const noexcept {
    return at_end() ? '\0' : source[pos];
  }

  void skip_spaces() {
    while (!at_end() && (source[pos] == ' ' || source[pos] == '\t' ||
                         source[pos] == '\n' || source[pos] == '\r')) {
      ++pos;
    }
  }

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  make_node(MathNodeKind kind, std::string value = {},
            std::vector<MathNode> children = {}) {
    if (nodes >= limits.max_ast_nodes) {
      return std::unexpected(make_budget("math.ast.nodes_exceeded",
                                         "Math AST exceeds max_ast_nodes")
                                 .with_json_path(offset_path(pos)));
    }
    ++nodes;
    return MathNode{.kind = kind,
                    .value = std::move(value),
                    .children = std::move(children)};
  }

  [[nodiscard]] std::expected<void, diagnostics::Error>
  ensure_depth(std::uint32_t depth) const {
    if (depth > limits.max_ast_depth) {
      return std::unexpected(make_budget("math.ast.depth_exceeded",
                                         "Math AST exceeds max_ast_depth")
                                 .with_json_path(offset_path(pos)));
    }
    return {};
  }

  [[nodiscard]] std::expected<std::string_view, diagnostics::Error>
  read_command_name() {
    if (at_end() || peek() != '\\') {
      return std::unexpected(make_invalid("math.parse.expected_command",
                                          "Expected a TeX-like command")
                                 .with_json_path(offset_path(pos)));
    }
    const std::size_t start = pos;
    ++pos;
    if (at_end()) {
      return std::unexpected(
          make_invalid("math.command.incomplete",
                       "Trailing backslash in math expression")
              .with_json_path(offset_path(start)));
    }
    if (is_letter(source[pos])) {
      const std::size_t name_start = pos;
      while (!at_end() && is_letter(source[pos])) {
        ++pos;
      }
      return source.substr(name_start, pos - name_start);
    }
    const std::string_view name = source.substr(pos, 1);
    ++pos;
    return name;
  }

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_braced(std::uint32_t depth) {
    if (peek() != '{') {
      return std::unexpected(make_invalid("math.parse.expected_brace",
                                          "Expected '{' group argument")
                                 .with_json_path(offset_path(pos)));
    }
    ++pos;
    auto body = parse_row(depth, /*stop_at_right=*/false);
    if (!body) {
      return std::unexpected(body.error());
    }
    if (peek() != '}') {
      return std::unexpected(make_invalid("math.brace.unbalanced",
                                          "Unclosed brace in math expression")
                                 .with_json_path(offset_path(pos)));
    }
    ++pos;
    return make_node(MathNodeKind::group, {}, {std::move(*body)});
  }

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_script_arg(std::uint32_t depth) {
    skip_spaces();
    if (peek() == '{') {
      return parse_braced(depth);
    }
    // Single nucleus without further scripts on the script argument itself.
    return parse_nucleus(depth, /*allow_scripts=*/false);
  }

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_nucleus(std::uint32_t depth, bool allow_scripts);

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_item(std::uint32_t depth) {
    auto nucleus = parse_nucleus(depth, /*allow_scripts=*/true);
    if (!nucleus) {
      return nucleus;
    }
    return nucleus;
  }

  // stop_char: when non-zero, end the row before consuming that character.
  // stop_at_right: end the row before a `\right` command.
  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_row(std::uint32_t depth, bool stop_at_right, char stop_char = '\0') {
    if (auto ok = ensure_depth(depth); !ok) {
      return std::unexpected(ok.error());
    }
    auto row = make_node(MathNodeKind::row);
    if (!row) {
      return row;
    }
    while (!at_end()) {
      skip_spaces();
      if (at_end()) {
        break;
      }
      const char ch = peek();
      if (stop_char != '\0' && ch == stop_char) {
        break;
      }
      if (ch == '}') {
        break;
      }
      if (stop_at_right && ch == '\\') {
        const std::size_t saved = pos;
        auto name = read_command_name();
        if (!name) {
          return std::unexpected(name.error());
        }
        pos = saved;
        if (*name == "right") {
          break;
        }
      }
      if (row->children.size() >= limits.max_row_length) {
        return std::unexpected(make_budget("math.ast.row_exceeded",
                                           "Math row exceeds max_row_length")
                                   .with_json_path(offset_path(pos)));
      }
      auto item = parse_item(depth);
      if (!item) {
        return std::unexpected(item.error());
      }
      row->children.push_back(std::move(*item));
    }
    return row;
  }

  [[nodiscard]] std::expected<MathNode, diagnostics::Error>
  parse_left_right(std::uint32_t depth) {
    // Caller already consumed `\left`.
    skip_spaces();
    if (at_end()) {
      return std::unexpected(make_invalid("math.parse.expected_delimiter",
                                          "Expected delimiter after \\left")
                                 .with_json_path(offset_path(pos)));
    }
    std::string open;
    if (peek() == '\\') {
      auto name = read_command_name();
      if (!name) {
        return std::unexpected(name.error());
      }
      open = "\\" + std::string(*name);
    } else {
      open.assign(1, peek());
      ++pos;
    }
    auto body = parse_row(depth + 1, /*stop_at_right=*/true);
    if (!body) {
      return std::unexpected(body.error());
    }
    skip_spaces();
    auto right = read_command_name();
    if (!right) {
      return std::unexpected(right.error());
    }
    if (*right != "right") {
      return std::unexpected(make_invalid("math.parse.expected_right",
                                          "Expected \\right to close \\left")
                                 .with_json_path(offset_path(pos)));
    }
    skip_spaces();
    if (at_end()) {
      return std::unexpected(make_invalid("math.parse.expected_delimiter",
                                          "Expected delimiter after \\right")
                                 .with_json_path(offset_path(pos)));
    }
    std::string close;
    if (peek() == '\\') {
      auto name = read_command_name();
      if (!name) {
        return std::unexpected(name.error());
      }
      close = "\\" + std::string(*name);
    } else {
      close.assign(1, peek());
      ++pos;
    }
    return make_node(MathNodeKind::delimited, open + "," + close,
                     {std::move(*body)});
  }
};

std::expected<MathNode, diagnostics::Error>
Parser::parse_nucleus(std::uint32_t depth, bool allow_scripts) {
  if (auto ok = ensure_depth(depth); !ok) {
    return std::unexpected(ok.error());
  }
  skip_spaces();
  if (at_end()) {
    return std::unexpected(make_invalid("math.parse.unexpected_end",
                                        "Unexpected end of math expression")
                               .with_json_path(offset_path(pos)));
  }

  MathNode nucleus;
  const char ch = peek();
  if (ch == '{') {
    auto group = parse_braced(depth + 1);
    if (!group) {
      return group;
    }
    nucleus = std::move(*group);
  } else if (ch == '\\') {
    const std::size_t cmd_at = pos;
    auto name = read_command_name();
    if (!name) {
      return std::unexpected(name.error());
    }
    if (*name == "left") {
      auto delim = parse_left_right(depth);
      if (!delim) {
        return delim;
      }
      nucleus = std::move(*delim);
    } else if (*name == "right") {
      return std::unexpected(make_invalid("math.parse.unexpected_right",
                                          "\\right without matching \\left")
                                 .with_json_path(offset_path(cmd_at)));
    } else {
      const MathCommandDesc *desc = find_supported_math_command(*name);
      if (desc == nullptr) {
        // Grammar pre-scan should have caught this; keep fail-closed.
        return std::unexpected(
            make_invalid("math.command.unsupported",
                         "Unsupported math command \\" + std::string(*name))
                .with_json_path(offset_path(cmd_at)));
      }
      if (desc->kind == MathCommandKind::structure && *name == "frac") {
        auto num = parse_braced(depth + 1);
        if (!num) {
          return std::unexpected(num.error());
        }
        auto den = parse_braced(depth + 1);
        if (!den) {
          return std::unexpected(den.error());
        }
        auto node = make_node(MathNodeKind::frac, "frac",
                              {std::move(*num), std::move(*den)});
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else if (desc->kind == MathCommandKind::structure && *name == "sqrt") {
        MathNode index;
        bool has_index = false;
        skip_spaces();
        if (peek() == '[') {
          ++pos;
          auto idx_row =
              parse_row(depth + 1, /*stop_at_right=*/false, /*stop_char=*/']');
          if (!idx_row) {
            return std::unexpected(idx_row.error());
          }
          if (peek() != ']') {
            return std::unexpected(make_invalid("math.parse.expected_bracket",
                                                "Expected ']' after sqrt index")
                                       .with_json_path(offset_path(pos)));
          }
          ++pos;
          index = std::move(*idx_row);
          has_index = true;
        }
        auto body = parse_braced(depth + 1);
        if (!body) {
          return std::unexpected(body.error());
        }
        std::vector<MathNode> kids;
        if (has_index) {
          kids.push_back(std::move(index));
        }
        kids.push_back(std::move(*body));
        auto node = make_node(MathNodeKind::sqrt, "sqrt", std::move(kids));
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else if (desc->kind == MathCommandKind::accent) {
        auto body = parse_braced(depth + 1);
        if (!body) {
          return std::unexpected(body.error());
        }
        auto node = make_node(MathNodeKind::accent, std::string(*name),
                              {std::move(*body)});
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else if (desc->kind == MathCommandKind::style) {
        auto body = parse_braced(depth + 1);
        if (!body) {
          return std::unexpected(body.error());
        }
        auto node = make_node(MathNodeKind::style, std::string(*name),
                              {std::move(*body)});
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else if (desc->kind == MathCommandKind::spacing) {
        auto node = make_node(MathNodeKind::space, std::string(*name));
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else if (desc->kind == MathCommandKind::literal_escape) {
        auto node = make_node(MathNodeKind::literal, std::string(*name));
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      } else {
        auto node = make_node(MathNodeKind::symbol, std::string(*name));
        if (!node) {
          return node;
        }
        nucleus = std::move(*node);
      }
    }
  } else if (is_letter(ch)) {
    auto node = make_node(MathNodeKind::atom, std::string(1, ch));
    if (!node) {
      return node;
    }
    ++pos;
    nucleus = std::move(*node);
  } else if (is_digit(ch)) {
    const std::size_t start = pos;
    while (!at_end() && is_digit(source[pos])) {
      ++pos;
    }
    auto node = make_node(MathNodeKind::number,
                          std::string(source.substr(start, pos - start)));
    if (!node) {
      return node;
    }
    nucleus = std::move(*node);
  } else if (ch == '}' || ch == ']') {
    return std::unexpected(
        make_invalid("math.parse.unexpected_token",
                     "Unexpected closing delimiter in math expression")
            .with_json_path(offset_path(pos)));
  } else {
    // Ordinary literal punctuation / operators / delimiters.
    auto node = make_node(MathNodeKind::literal, std::string(1, ch));
    if (!node) {
      return node;
    }
    ++pos;
    nucleus = std::move(*node);
  }

  if (!allow_scripts) {
    return nucleus;
  }

  bool has_sub = false;
  bool has_sup = false;
  MathNode sub = MathNode{.kind = MathNodeKind::row};
  MathNode sup = MathNode{.kind = MathNodeKind::row};
  // Empty placeholders do not count toward the node budget until attached.
  for (;;) {
    skip_spaces();
    if (peek() == '_') {
      if (has_sub) {
        return std::unexpected(
            make_invalid("math.parse.duplicate_subscript",
                         "Duplicate subscript on math nucleus")
                .with_json_path(offset_path(pos)));
      }
      ++pos;
      auto arg = parse_script_arg(depth + 1);
      if (!arg) {
        return std::unexpected(arg.error());
      }
      sub = std::move(*arg);
      has_sub = true;
      continue;
    }
    if (peek() == '^') {
      if (has_sup) {
        return std::unexpected(
            make_invalid("math.parse.duplicate_superscript",
                         "Duplicate superscript on math nucleus")
                .with_json_path(offset_path(pos)));
      }
      ++pos;
      auto arg = parse_script_arg(depth + 1);
      if (!arg) {
        return std::unexpected(arg.error());
      }
      sup = std::move(*arg);
      has_sup = true;
      continue;
    }
    break;
  }

  if (!has_sub && !has_sup) {
    return nucleus;
  }

  // Account for placeholder empty rows when a side is absent.
  if (!has_sub) {
    auto empty = make_node(MathNodeKind::row);
    if (!empty) {
      return empty;
    }
    sub = std::move(*empty);
  }
  if (!has_sup) {
    auto empty = make_node(MathNodeKind::row);
    if (!empty) {
      return empty;
    }
    sup = std::move(*empty);
  }
  return make_node(MathNodeKind::script, {},
                   {std::move(nucleus), std::move(sub), std::move(sup)});
}

} // namespace

std::expected<MathNode, diagnostics::Error>
parse_math(std::string_view source, const MathParseLimits &limits) {
  if (limits.max_ast_nodes == 0 || limits.max_ast_depth == 0 ||
      limits.max_row_length == 0) {
    return std::unexpected(make_invalid("math.limits.invalid",
                                        "Math parse limits must be positive"));
  }
  auto grammar = validate_math_grammar(source, limits.grammar);
  if (!grammar) {
    return std::unexpected(grammar.error());
  }

  Parser parser{.source = source, .limits = limits};
  auto root = parser.parse_row(1, /*stop_at_right=*/false);
  if (!root) {
    return std::unexpected(root.error());
  }
  parser.skip_spaces();
  if (!parser.at_end()) {
    return std::unexpected(make_invalid("math.parse.trailing_input",
                                        "Trailing input after math expression")
                               .with_json_path(offset_path(parser.pos)));
  }
  return root;
}

} // namespace instantee::math
