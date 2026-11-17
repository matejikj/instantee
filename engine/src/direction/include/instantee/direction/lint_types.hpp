#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace instantee::direction {

// Creative linter severities (SAS §9.5). Aesthetic quality is never asserted as
// absolute truth — findings are measurable policy signals only.
enum class LintSeverity : std::uint8_t {
  info = 1,
  warning,
  error,
};

struct LintRect {
  double x_px = 0;
  double y_px = 0;
  double width_px = 0;
  double height_px = 0;
};

struct LintFinding {
  std::string code;
  LintSeverity severity = LintSeverity::warning;
  std::string path;
  std::string element_id;
  std::string message;
};

[[nodiscard]] constexpr std::string_view
lint_severity_id(LintSeverity severity) noexcept {
  switch (severity) {
  case LintSeverity::info:
    return "info";
  case LintSeverity::warning:
    return "warning";
  case LintSeverity::error:
    return "error";
  }
  return "unknown";
}

} // namespace instantee::direction
