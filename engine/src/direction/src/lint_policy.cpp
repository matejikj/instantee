#include <instantee/direction/lint_policy.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

namespace instantee::direction {
namespace {

diagnostics::Error make_error(std::string_view code, std::string_view message,
                              std::string_view json_path = {}) {
  auto out = diagnostics::Error{diagnostics::ExitCode::invalid_input,
                                diagnostics::ErrorKind::invalid_input, code,
                                "lint_policy", message};
  if (!json_path.empty()) {
    out = std::move(out).with_json_path(std::string{json_path});
  }
  return out;
}

[[nodiscard]] std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char ch : text) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
      out.push_back(static_cast<char>(ch));
    } else if (ch < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
      out.append(buf);
    } else {
      out.push_back(static_cast<char>(ch));
    }
  }
  return out;
}

[[nodiscard]] std::expected<std::string, diagnostics::Error>
read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(
        make_error("io_error", "Cannot open lint policy input file"));
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::expected<LintSeverity, diagnostics::Error>
parse_severity(std::string_view text, std::string_view path) {
  if (text == "info") {
    return LintSeverity::info;
  }
  if (text == "warning") {
    return LintSeverity::warning;
  }
  if (text == "error") {
    return LintSeverity::error;
  }
  return std::unexpected(make_error(
      "invalid_enum", "Lint severity must be info, warning, or error", path));
}

[[nodiscard]] bool waiver_matches(const LintWaiver &waiver,
                                  const LintFinding &finding) noexcept {
  if (waiver.code != finding.code) {
    return false;
  }
  if (!waiver.path.empty() && waiver.path != finding.path) {
    return false;
  }
  if (!waiver.element_id.empty() && waiver.element_id != finding.element_id) {
    return false;
  }
  return true;
}

[[nodiscard]] std::string finding_json(const LintFinding &f) {
  std::string out = "{\"code\":\"";
  out += json_escape(f.code);
  out += "\",\"severity\":\"";
  out += lint_severity_id(f.severity);
  out += "\",\"path\":\"";
  out += json_escape(f.path);
  out += "\",\"element_id\":\"";
  out += json_escape(f.element_id);
  out += "\",\"message\":\"";
  out += json_escape(f.message);
  out += "\"}";
  return out;
}

[[nodiscard]] std::string waiver_json(const LintWaiver &w) {
  std::string out = "{\"code\":\"";
  out += json_escape(w.code);
  out += "\",\"path\":\"";
  out += json_escape(w.path);
  out += "\",\"element_id\":\"";
  out += json_escape(w.element_id);
  out += "\",\"reason\":\"";
  out += json_escape(w.reason);
  out += "\",\"approval_id\":\"";
  out += json_escape(w.approval_id);
  out += "\"}";
  return out;
}

struct DocGuard {
  yyjson_doc *owned = nullptr;
  ~DocGuard() { yyjson_doc_free(owned); }
};

[[nodiscard]] std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *obj, const char *key, std::string_view path,
               bool required) {
  yyjson_val *val = yyjson_obj_get(obj, key);
  if (val == nullptr) {
    if (!required) {
      return std::string{};
    }
    return std::unexpected(
        make_error("missing_field", std::string("Missing field ") + key, path));
  }
  if (!yyjson_is_str(val)) {
    return std::unexpected(make_error(
        "type_mismatch", std::string(key) + " must be a string", path));
  }
  const char *text = yyjson_get_str(val);
  return text == nullptr ? std::string{} : std::string{text};
}

} // namespace

std::expected<LintEnforcementMode, diagnostics::Error>
parse_lint_enforcement(std::string_view text) noexcept {
  if (text == "off") {
    return LintEnforcementMode::off;
  }
  if (text == "warn") {
    return LintEnforcementMode::warn;
  }
  if (text == "strict") {
    return LintEnforcementMode::strict;
  }
  return std::unexpected(
      make_error("invalid_enum", "Lint policy must be off, warn, or strict"));
}

std::expected<LintCliOptions, diagnostics::Error>
parse_lint_cli_options(std::span<const std::string_view> args,
                       bool release_build) {
  LintCliOptions options;
  options.policy = default_lint_enforcement(release_build);
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "--lint-policy" || arg == "--policy") {
      if (i + 1 >= args.size()) {
        return std::unexpected(
            make_error("invalid_arguments", "Missing value for lint policy"));
      }
      auto mode = parse_lint_enforcement(args[++i]);
      if (!mode) {
        return std::unexpected(mode.error());
      }
      options.policy = *mode;
      options.policy_explicit = true;
      continue;
    }
    if (arg == "--lint-waivers" || arg == "--waivers") {
      if (i + 1 >= args.size()) {
        return std::unexpected(
            make_error("invalid_arguments", "Missing value for lint waivers"));
      }
      options.waivers_path = std::filesystem::path{std::string{args[++i]}};
      continue;
    }
    if (arg == "--lint-report" || arg == "--report") {
      if (i + 1 >= args.size()) {
        return std::unexpected(
            make_error("invalid_arguments", "Missing value for lint report"));
      }
      options.report_path = std::filesystem::path{std::string{args[++i]}};
      continue;
    }
    return std::unexpected(
        make_error("invalid_arguments",
                   "Unknown lint CLI option (expected --lint-policy/--policy, "
                   "--lint-waivers/--waivers, --lint-report/--report)"));
  }
  return options;
}

std::expected<std::vector<LintFinding>, diagnostics::Error>
load_lint_findings_json(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(make_error("empty_document", "Findings JSON empty"));
  }
  yyjson_doc *doc = yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG);
  if (doc == nullptr) {
    return std::unexpected(
        make_error("invalid_json", "Findings JSON parse failed"));
  }
  DocGuard guard{doc};
  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(
        make_error("type_mismatch", "Findings root must be an object"));
  }
  auto schema = require_string(root, "schema_version", "/schema_version", true);
  if (!schema) {
    return std::unexpected(schema.error());
  }
  if (*schema != "lint-findings/1") {
    return std::unexpected(make_error(
        "unsupported_schema", "Unsupported lint findings schema_version",
        "/schema_version"));
  }
  yyjson_val *arr = yyjson_obj_get(root, "findings");
  if (arr == nullptr || !yyjson_is_arr(arr)) {
    return std::unexpected(
        make_error("type_mismatch", "findings must be an array", "/findings"));
  }
  std::vector<LintFinding> out;
  out.reserve(yyjson_arr_size(arr));
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(arr, &iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&iter)) {
    const std::string base = "/findings/" + std::to_string(index++);
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_error("type_mismatch", "Finding must be an object", base));
    }
    auto code = require_string(item, "code", base + "/code", true);
    auto severity_s =
        require_string(item, "severity", base + "/severity", true);
    auto path = require_string(item, "path", base + "/path", true);
    auto element =
        require_string(item, "element_id", base + "/element_id", true);
    auto message = require_string(item, "message", base + "/message", true);
    if (!code || !severity_s || !path || !element || !message) {
      return std::unexpected((!code         ? code.error()
                              : !severity_s ? severity_s.error()
                              : !path       ? path.error()
                              : !element    ? element.error()
                                            : message.error()));
    }
    if (code->empty() || path->empty() || element->empty()) {
      return std::unexpected(
          make_error("invalid_finding",
                     "Finding code/path/element_id must be non-empty", base));
    }
    auto severity = parse_severity(*severity_s, base + "/severity");
    if (!severity) {
      return std::unexpected(severity.error());
    }
    out.push_back(LintFinding{.code = std::move(*code),
                              .severity = *severity,
                              .path = std::move(*path),
                              .element_id = std::move(*element),
                              .message = std::move(*message)});
  }
  return out;
}

std::expected<std::vector<LintFinding>, diagnostics::Error>
load_lint_findings_file(const std::filesystem::path &path) {
  auto text = read_file(path);
  if (!text) {
    return std::unexpected(text.error());
  }
  return load_lint_findings_json(*text);
}

std::expected<std::vector<LintWaiver>, diagnostics::Error>
load_lint_waivers_json(std::string_view text) {
  if (text.empty()) {
    return std::unexpected(make_error("empty_document", "Waivers JSON empty"));
  }
  yyjson_doc *doc = yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG);
  if (doc == nullptr) {
    return std::unexpected(
        make_error("invalid_json", "Waivers JSON parse failed"));
  }
  DocGuard guard{doc};
  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(
        make_error("type_mismatch", "Waivers root must be an object"));
  }
  auto schema = require_string(root, "schema_version", "/schema_version", true);
  if (!schema) {
    return std::unexpected(schema.error());
  }
  if (*schema != "lint-waivers/1") {
    return std::unexpected(make_error("unsupported_schema",
                                      "Unsupported lint waivers schema_version",
                                      "/schema_version"));
  }
  yyjson_val *arr = yyjson_obj_get(root, "waivers");
  if (arr == nullptr || !yyjson_is_arr(arr)) {
    return std::unexpected(
        make_error("type_mismatch", "waivers must be an array", "/waivers"));
  }
  std::vector<LintWaiver> out;
  out.reserve(yyjson_arr_size(arr));
  std::size_t index = 0;
  yyjson_arr_iter iter;
  yyjson_arr_iter_init(arr, &iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&iter)) {
    const std::string base = "/waivers/" + std::to_string(index++);
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_error("type_mismatch", "Waiver must be an object", base));
    }
    auto code = require_string(item, "code", base + "/code", true);
    auto path = require_string(item, "path", base + "/path", false);
    auto element =
        require_string(item, "element_id", base + "/element_id", false);
    auto reason = require_string(item, "reason", base + "/reason", true);
    auto approval =
        require_string(item, "approval_id", base + "/approval_id", true);
    if (!code || !path || !element || !reason || !approval) {
      return std::unexpected((!code      ? code.error()
                              : !path    ? path.error()
                              : !element ? element.error()
                              : !reason  ? reason.error()
                                         : approval.error()));
    }
    if (code->empty() || reason->empty() || approval->empty()) {
      return std::unexpected(make_error(
          "invalid_waiver",
          "Waiver code, reason, and approval_id must be non-empty", base));
    }
    // Keep audit fields free of obvious personal emails.
    if (approval->find('@') != std::string::npos ||
        reason->find('@') != std::string::npos) {
      return std::unexpected(make_error(
          "invalid_waiver",
          "Waiver reason/approval_id must not contain email addresses", base));
    }
    out.push_back(LintWaiver{.code = std::move(*code),
                             .path = std::move(*path),
                             .element_id = std::move(*element),
                             .reason = std::move(*reason),
                             .approval_id = std::move(*approval)});
  }
  return out;
}

std::expected<std::vector<LintWaiver>, diagnostics::Error>
load_lint_waivers_file(const std::filesystem::path &path) {
  auto text = read_file(path);
  if (!text) {
    return std::unexpected(text.error());
  }
  return load_lint_waivers_json(*text);
}

std::expected<LintPolicyReport, diagnostics::Error>
evaluate_lint_policy(std::span<const LintFinding> findings,
                     LintEnforcementMode policy,
                     std::span<const LintWaiver> waivers, bool release_build) {
  for (const LintFinding &f : findings) {
    if (f.code.empty() || f.path.empty() || f.element_id.empty()) {
      return std::unexpected(
          make_error("invalid_finding",
                     "Findings require non-empty code, path, and element_id"));
    }
  }
  for (const LintWaiver &w : waivers) {
    if (w.code.empty() || w.reason.empty() || w.approval_id.empty()) {
      return std::unexpected(make_error(
          "invalid_waiver",
          "Waivers require non-empty code, reason, and approval_id"));
    }
  }

  LintPolicyReport report;
  report.policy = policy;
  report.release_build = release_build;
  report.findings.assign(findings.begin(), findings.end());

  std::vector<bool> waiver_used(waivers.size(), false);
  for (const LintFinding &finding : findings) {
    std::optional<std::size_t> matched;
    for (std::size_t i = 0; i < waivers.size(); ++i) {
      if (waiver_matches(waivers[i], finding)) {
        matched = i;
        break;
      }
    }
    if (matched) {
      waiver_used[*matched] = true;
      report.waived.push_back(
          LintWaivedFinding{.finding = finding, .waiver = waivers[*matched]});
      continue;
    }
    if (policy == LintEnforcementMode::strict &&
        finding.severity == LintSeverity::error) {
      report.blocking.push_back(finding);
    }
  }

  for (std::size_t i = 0; i < waivers.size(); ++i) {
    if (!waiver_used[i]) {
      report.waivers_unused.push_back(waivers[i]);
    }
  }

  // Deterministic report ordering for contract tests.
  auto by_path_code = [](const LintFinding &a, const LintFinding &b) {
    if (a.path != b.path) {
      return a.path < b.path;
    }
    if (a.code != b.code) {
      return a.code < b.code;
    }
    return a.element_id < b.element_id;
  };
  std::sort(report.findings.begin(), report.findings.end(), by_path_code);
  std::sort(report.blocking.begin(), report.blocking.end(), by_path_code);
  std::sort(report.waived.begin(), report.waived.end(),
            [](const LintWaivedFinding &a, const LintWaivedFinding &b) {
              if (a.finding.path != b.finding.path) {
                return a.finding.path < b.finding.path;
              }
              if (a.finding.code != b.finding.code) {
                return a.finding.code < b.finding.code;
              }
              return a.waiver.approval_id < b.waiver.approval_id;
            });
  std::sort(report.waivers_unused.begin(), report.waivers_unused.end(),
            [](const LintWaiver &a, const LintWaiver &b) {
              if (a.code != b.code) {
                return a.code < b.code;
              }
              return a.approval_id < b.approval_id;
            });

  // off/warn never fail the process; strict fails on unwaived errors.
  report.ok = policy != LintEnforcementMode::strict || report.blocking.empty();
  return report;
}

std::string lint_policy_report_json(const LintPolicyReport &report) {
  std::string out = "{\"schema_version\":\"lint-policy-report/1\",\"ok\":";
  out += report.ok ? "true" : "false";
  out += ",\"policy\":\"";
  out += lint_enforcement_id(report.policy);
  out += "\",\"release_build\":";
  out += report.release_build ? "true" : "false";
  out += ",\"findings\":[";
  for (std::size_t i = 0; i < report.findings.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += finding_json(report.findings[i]);
  }
  out += "],\"blocking\":[";
  for (std::size_t i = 0; i < report.blocking.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += finding_json(report.blocking[i]);
  }
  out += "],\"waived\":[";
  for (std::size_t i = 0; i < report.waived.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += "{\"finding\":";
    out += finding_json(report.waived[i].finding);
    out += ",\"waiver\":";
    out += waiver_json(report.waived[i].waiver);
    out += '}';
  }
  out += "],\"waivers_unused\":[";
  for (std::size_t i = 0; i < report.waivers_unused.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += waiver_json(report.waivers_unused[i]);
  }
  out += "]}";
  return out;
}

int lint_policy_exit_code(const LintPolicyReport &report) noexcept {
  return report.ok ? 0 : static_cast<int>(diagnostics::ExitCode::invalid_input);
}

} // namespace instantee::direction
