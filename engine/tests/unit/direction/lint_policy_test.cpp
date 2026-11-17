#include <instantee/direction/lint_policy.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using instantee::direction::default_lint_enforcement;
using instantee::direction::evaluate_lint_policy;
using instantee::direction::lint_enforcement_id;
using instantee::direction::lint_policy_exit_code;
using instantee::direction::lint_policy_report_json;
using instantee::direction::LintEnforcementMode;
using instantee::direction::LintFinding;
using instantee::direction::LintSeverity;
using instantee::direction::LintWaiver;
using instantee::direction::load_lint_findings_file;
using instantee::direction::load_lint_waivers_file;
using instantee::direction::load_lint_waivers_json;
using instantee::direction::parse_lint_cli_options;
using instantee::direction::parse_lint_enforcement;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] LintFinding error_overlap() {
  return LintFinding{.code = "hard_overlap",
                     .severity = LintSeverity::error,
                     .path = "/shots/0/nodes/title",
                     .element_id = "title",
                     .message = "Important elements overlap: title and cta"};
}

[[nodiscard]] LintWaiver approved_overlap() {
  return LintWaiver{.code = "hard_overlap",
                    .path = "/shots/0/nodes/title",
                    .element_id = "title",
                    .reason =
                        "Intentional stacked kinetic type for the opener hold",
                    .approval_id = "waiver-fixture-001"};
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: lint_policy_test <source_root>\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root = argv[1];
  const auto findings_path =
      root / "contracts/fixtures/lint-policy/findings-with-error.json";
  const auto waiver_path =
      root / "contracts/fixtures/lint-policy/waiver-hard-overlap.json";
  expect(std::filesystem::exists(findings_path), "findings fixture");
  expect(std::filesystem::exists(waiver_path), "waiver fixture");

  expect(default_lint_enforcement(true) == LintEnforcementMode::strict,
         "release default is strict");
  expect(default_lint_enforcement(false) == LintEnforcementMode::warn,
         "non-release default is warn");
  expect(std::string(lint_enforcement_id(LintEnforcementMode::strict)) ==
             "strict",
         "policy id");

  auto mode = parse_lint_enforcement("strict");
  expect(mode.has_value() && *mode == LintEnforcementMode::strict,
         "parse strict");
  expect(!parse_lint_enforcement("loud").has_value(), "reject unknown policy");

  // CLI contract: short and long option names.
  {
    const std::array<std::string_view, 6> args = {
        "--policy", "strict", "--waivers", "w.json", "--report", "r.json"};
    auto opts = parse_lint_cli_options(args, /*release_build=*/false);
    expect(opts.has_value(), "parse lint cli options");
    if (opts) {
      expect(opts->policy == LintEnforcementMode::strict, "cli policy");
      expect(opts->policy_explicit, "cli policy explicit");
      expect(opts->waivers_path && opts->waivers_path->string() == "w.json",
             "cli waivers");
      expect(opts->report_path && opts->report_path->string() == "r.json",
             "cli report");
    }
    auto release_default = parse_lint_cli_options({}, true);
    expect(release_default.has_value() &&
               release_default->policy == LintEnforcementMode::strict &&
               !release_default->policy_explicit,
           "release CLI default strict");
  }

  auto findings = load_lint_findings_file(findings_path);
  expect(findings.has_value() && findings->size() == 3, "load findings");
  auto waivers = load_lint_waivers_file(waiver_path);
  expect(waivers.has_value() && waivers->size() == 1, "load waivers");
  if (!findings || !waivers) {
    return EXIT_FAILURE;
  }

  // Release/strict rejects unwaived severity=error.
  {
    auto rejected = evaluate_lint_policy(*findings, LintEnforcementMode::strict,
                                         {}, /*release_build=*/true);
    expect(rejected.has_value(), "strict eval");
    if (rejected) {
      expect(!rejected->ok, "strict rejects error");
      expect(rejected->blocking.size() == 1, "one blocker");
      expect(lint_policy_exit_code(*rejected) == 2, "exit invalid_input");
      const std::string json = lint_policy_report_json(*rejected);
      expect(json.find("\"ok\":false") != std::string::npos, "json ok false");
      expect(json.find("\"release_build\":true") != std::string::npos,
             "json release");
      expect(json.find("hard_overlap") != std::string::npos, "json code");
    }
  }

  // Approved waiver is auditable and clears the blocker under strict.
  {
    auto accepted =
        evaluate_lint_policy(*findings, LintEnforcementMode::strict, *waivers,
                             /*release_build=*/true);
    expect(accepted.has_value() && accepted->ok, "waived strict ok");
    if (accepted) {
      expect(accepted->blocking.empty(), "no blockers after waiver");
      expect(accepted->waived.size() == 1, "one waived finding");
      expect(accepted->waived.front().waiver.approval_id ==
                 "waiver-fixture-001",
             "approval id audited");
      expect(accepted->waived.front().waiver.reason.find("Intentional") !=
                 std::string::npos,
             "reason audited");
      expect(lint_policy_exit_code(*accepted) == 0, "exit success");
      const std::string json = lint_policy_report_json(*accepted);
      expect(json.find("\"ok\":true") != std::string::npos, "json ok true");
      expect(json.find("waiver-fixture-001") != std::string::npos,
             "json approval");
      expect(json.find("Intentional stacked") != std::string::npos,
             "json reason");
    }
  }

  // warn mode never fails the process on findings.
  {
    auto warned = evaluate_lint_policy(std::array{error_overlap()},
                                       LintEnforcementMode::warn, {}, false);
    expect(warned.has_value() && warned->ok && warned->blocking.empty(),
           "warn does not block");
  }

  // Waiver without reason is rejected.
  {
    LintWaiver bad = approved_overlap();
    bad.reason.clear();
    auto err = evaluate_lint_policy(std::array{error_overlap()},
                                    LintEnforcementMode::strict,
                                    std::array{bad}, true);
    expect(!err.has_value(), "empty reason rejected");
  }

  // Email in approval_id is rejected at load time.
  {
    auto bad = load_lint_waivers_json(
        R"({"schema_version":"lint-waivers/1","waivers":[{"code":"hard_overlap","reason":"ok","approval_id":"person@example.com"}]})");
    expect(!bad.has_value(), "email approval rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
