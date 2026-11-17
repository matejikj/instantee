#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/lint_types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// CLI / compile enforcement for creative lint findings (SAS §9.5).
enum class LintEnforcementMode : std::uint8_t {
  off = 1, // report only; never fail
  warn,    // report; fail only on programmer/input errors, not findings
  strict,  // fail on unwaived severity=error findings
};

struct LintWaiver {
  // Finding code to waive (required).
  std::string code;
  // Empty path/element matches any finding with the same code.
  std::string path;
  std::string element_id;
  // Human-readable justification (required, non-secret).
  std::string reason;
  // Opaque approval reference for audit (no personal email/name).
  std::string approval_id;
};

struct LintCliOptions {
  LintEnforcementMode policy = LintEnforcementMode::warn;
  bool policy_explicit = false;
  std::optional<std::filesystem::path> waivers_path;
  std::optional<std::filesystem::path> report_path;
};

struct LintWaivedFinding {
  LintFinding finding;
  LintWaiver waiver;
};

struct LintPolicyReport {
  LintEnforcementMode policy = LintEnforcementMode::warn;
  bool release_build = false;
  bool ok = true;
  std::vector<LintFinding> findings;
  std::vector<LintFinding> blocking;
  std::vector<LintWaivedFinding> waived;
  std::vector<LintWaiver> waivers_unused;
};

// Release configuration defaults to strict; non-release defaults to warn.
[[nodiscard]] constexpr LintEnforcementMode
default_lint_enforcement(bool release_build) noexcept {
  return release_build ? LintEnforcementMode::strict
                       : LintEnforcementMode::warn;
}

[[nodiscard]] constexpr bool lint_release_build_default() noexcept {
#if defined(NDEBUG)
  return true;
#else
  return false;
#endif
}

[[nodiscard]] constexpr std::string_view
lint_enforcement_id(LintEnforcementMode mode) noexcept {
  switch (mode) {
  case LintEnforcementMode::off:
    return "off";
  case LintEnforcementMode::warn:
    return "warn";
  case LintEnforcementMode::strict:
    return "strict";
  }
  return "unknown";
}

[[nodiscard]] std::expected<LintEnforcementMode, diagnostics::Error>
parse_lint_enforcement(std::string_view text) noexcept;

// Parse `--lint-policy`, `--lint-waivers`, `--lint-report` from a trailing
// option list. Unknown flags fail. When policy is omitted, uses
// default_lint_enforcement(release_build).
[[nodiscard]] std::expected<LintCliOptions, diagnostics::Error>
parse_lint_cli_options(std::span<const std::string_view> args,
                       bool release_build = lint_release_build_default());

[[nodiscard]] std::expected<std::vector<LintFinding>, diagnostics::Error>
load_lint_findings_json(std::string_view text);

[[nodiscard]] std::expected<std::vector<LintFinding>, diagnostics::Error>
load_lint_findings_file(const std::filesystem::path &path);

[[nodiscard]] std::expected<std::vector<LintWaiver>, diagnostics::Error>
load_lint_waivers_json(std::string_view text);

[[nodiscard]] std::expected<std::vector<LintWaiver>, diagnostics::Error>
load_lint_waivers_file(const std::filesystem::path &path);

// Apply waivers and enforcement. Strict mode rejects unwaived errors.
[[nodiscard]] std::expected<LintPolicyReport, diagnostics::Error>
evaluate_lint_policy(std::span<const LintFinding> findings,
                     LintEnforcementMode policy,
                     std::span<const LintWaiver> waivers,
                     bool release_build = false);

[[nodiscard]] std::string
lint_policy_report_json(const LintPolicyReport &report);

// Process exit status for a finished policy decision (0 or 2/invalid_input).
[[nodiscard]] int
lint_policy_exit_code(const LintPolicyReport &report) noexcept;

} // namespace instantee::direction
