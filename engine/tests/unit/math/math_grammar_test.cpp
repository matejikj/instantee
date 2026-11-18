#include <instantee/math/grammar.hpp>

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

using instantee::math::find_supported_math_command;
using instantee::math::is_forbidden_math_command;
using instantee::math::MathCommandKind;
using instantee::math::MathGrammarLimits;
using instantee::math::supported_math_commands;
using instantee::math::validate_math_grammar;

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

// Minimal flat-string extractor for math-grammar fixtures (ASCII JSON).
[[nodiscard]] std::optional<std::string>
json_string_field(std::string_view text, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const auto key_pos = text.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  auto pos = text.find(':', key_pos + needle.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                               text[pos] == '\t' || text[pos] == '\r')) {
    ++pos;
  }
  if (pos >= text.size() || text[pos] != '"') {
    return std::nullopt;
  }
  ++pos;
  std::string out;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      return out;
    }
    if (ch == '\\' && pos < text.size()) {
      const char esc = text[pos++];
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        out.push_back(esc);
        break;
      }
      continue;
    }
    out.push_back(ch);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<bool> json_bool_field(std::string_view text,
                                                  std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const auto key_pos = text.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  auto pos = text.find(':', key_pos + needle.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                               text[pos] == '\t' || text[pos] == '\r')) {
    ++pos;
  }
  if (text.substr(pos, 4) == "true") {
    return true;
  }
  if (text.substr(pos, 5) == "false") {
    return false;
  }
  return std::nullopt;
}

struct FixtureCase {
  std::filesystem::path path;
  std::string source;
  bool expect_ok = false;
  std::string expect_code;
};

[[nodiscard]] std::vector<FixtureCase>
load_fixtures(const std::filesystem::path &dir) {
  std::vector<FixtureCase> out;
  if (!std::filesystem::exists(dir)) {
    return out;
  }
  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {
      continue;
    }
    const std::string text = read_file(entry.path());
    auto source = json_string_field(text, "source");
    auto ok = json_bool_field(text, "expect_ok");
    if (!source || !ok) {
      continue;
    }
    FixtureCase fx;
    fx.path = entry.path();
    fx.source = *source;
    fx.expect_ok = *ok;
    if (auto code = json_string_field(text, "expect_code")) {
      fx.expect_code = *code;
    }
    out.push_back(std::move(fx));
  }
  return out;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: math_grammar_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto adr =
      root / "motion_framework_sas/adrs/0015-math-tex-subset-v1.md";
  expect(std::filesystem::exists(adr), "ADR-0015 present");

  const auto commands = supported_math_commands();
  expect(!commands.empty(), "non-empty allowlist");
  for (std::size_t i = 1; i < commands.size(); ++i) {
    expect(commands[i - 1].name < commands[i].name, "allowlist sorted");
  }
  expect(find_supported_math_command("frac") != nullptr &&
             find_supported_math_command("frac")->kind ==
                 MathCommandKind::structure &&
             find_supported_math_command("frac")->arity == 2,
         "frac mapping");
  expect(find_supported_math_command("unknowncmd") == nullptr,
         "unknown absent");
  expect(is_forbidden_math_command("input"), "input forbidden");
  expect(is_forbidden_math_command("write18"), "write18 forbidden");
  expect(!is_forbidden_math_command("frac"), "frac not forbidden");

  // Module owns only diagnostics — no TeX process surface to call.
  expect(validate_math_grammar("a+b").has_value(), "plain ascii ok");
  expect(validate_math_grammar("\\frac{1}{2}").has_value(), "frac ok");

  auto unsupported = validate_math_grammar("\\foo");
  expect(!unsupported.has_value(), "unsupported rejects");
  if (!unsupported) {
    expect(unsupported.error().code == "math.command.unsupported",
           "unsupported code");
    expect(unsupported.error().phase == "math", "math phase");
    expect(unsupported.error().message.find("\\foo") != std::string::npos,
           "unsupported names command");
    expect(unsupported.error().context.json_path.has_value(),
           "unsupported offset path");
  }

  auto forbidden = validate_math_grammar("\\input{x}");
  expect(!forbidden.has_value(), "forbidden rejects");
  if (!forbidden) {
    expect(forbidden.error().code == "math.command.forbidden",
           "forbidden code");
    expect(forbidden.error().message.find("external TeX") != std::string::npos,
           "forbidden mentions no external TeX");
  }

  auto env = validate_math_grammar("\\begin{matrix}a\\end{matrix}");
  expect(!env.has_value() && env.error().code == "math.environment.unsupported",
         "environment rejects");

  MathGrammarLimits tiny;
  tiny.max_input_bytes = 4;
  expect(!validate_math_grammar("12345", tiny).has_value() &&
             validate_math_grammar("12345", tiny).error().code ==
                 "math.input.too_large",
         "size budget");

  MathGrammarLimits shallow;
  shallow.max_brace_depth = 1;
  expect(!validate_math_grammar("{{a}}", shallow).has_value() &&
             validate_math_grammar("{{a}}", shallow).error().code ==
                 "math.brace_depth.exceeded",
         "depth budget");

  MathGrammarLimits few;
  few.max_commands = 1;
  expect(!validate_math_grammar("\\alpha\\beta", few).has_value() &&
             validate_math_grammar("\\alpha\\beta", few).error().code ==
                 "math.commands.exceeded",
         "command budget");

  expect(!validate_math_grammar("{a").has_value(), "unclosed brace");
  expect(!validate_math_grammar("a}").has_value(), "extra brace");
  expect(!validate_math_grammar("\\").has_value(), "trailing backslash");

  const auto positive =
      load_fixtures(root / "contracts/fixtures/math-grammar/positive");
  const auto negative =
      load_fixtures(root / "contracts/fixtures/math-grammar/negative");
  expect(positive.size() >= 3, "positive fixtures present");
  expect(negative.size() >= 4, "negative fixtures present");

  for (const FixtureCase &fx : positive) {
    auto result = validate_math_grammar(fx.source);
    expect(result.has_value(), fx.path.filename().string().c_str());
  }
  for (const FixtureCase &fx : negative) {
    auto result = validate_math_grammar(fx.source);
    expect(!result.has_value(), fx.path.filename().string().c_str());
    if (!result && !fx.expect_code.empty()) {
      expect(result.error().code == fx.expect_code,
             (fx.path.filename().string() + " code").c_str());
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
