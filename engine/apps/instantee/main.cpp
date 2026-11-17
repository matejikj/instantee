#include <instantee/compiler/compile.hpp>
#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/integer_encoding.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/diagnostics/exception_boundary.hpp>
#include <instantee/direction/lint_policy.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/frame_preview.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using instantee::diagnostics::Error;
using instantee::diagnostics::ExitCode;

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

int fail(const Error &error) {
  std::fprintf(stderr, "{\"code\":\"%s\",\"phase\":\"%s\",\"message\":\"%s\"",
               json_escape(error.code).c_str(),
               json_escape(error.phase).c_str(),
               json_escape(error.message).c_str());
  if (error.context.json_path) {
    std::fprintf(stderr, ",\"json_path\":\"%s\"",
                 json_escape(*error.context.json_path).c_str());
  }
  if (error.context.frame) {
    std::fprintf(stderr, ",\"frame\":%llu",
                 static_cast<unsigned long long>(*error.context.frame));
  }
  if (error.context.shot) {
    std::fprintf(stderr, ",\"shot\":\"%s\"",
                 json_escape(*error.context.shot).c_str());
  }
  if (error.context.node) {
    std::fprintf(stderr, ",\"node\":\"%s\"",
                 json_escape(*error.context.node).c_str());
  }
  std::fprintf(stderr, ",\"published\":false}\n");
  return static_cast<int>(error.exit_code);
}

void usage() {
  std::fputs(
      "usage:\n"
      "  instantee validate <authoring.json> [--style-bible <bible.json>]\n"
      "  instantee compile <authoring.json> --out <scene.json> "
      "[--direction-report <report.txt>] [--style-bible <bible.json>]\n"
      "  instantee direction-report <authoring.json> [--out <report.txt>]\n"
      "  instantee frame <scene.json> --at <N> [--out "
      "<report.json|preview.png>]\n"
      "  instantee lint <findings.json> [--policy off|warn|strict] "
      "[--waivers <waivers.json>] [--report <report.json>]\n"
      "  instantee help\n"
      "\n"
      "Lint policy defaults to strict in release builds and warn otherwise. "
      "Strict rejects unwaived severity=error findings. Optional Style Bible "
      "path loads tokens during CDS style resolve and timed lint.\n",
      stdout);
}

std::expected<std::string, Error> read_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(Error{ExitCode::invalid_input, "io_error", "cli",
                                 "Cannot open input file"});
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void print_ok(std::string_view command,
              const instantee::compiler::CompileStageTiming &timing,
              std::uint64_t duration_frames, std::string_view out_path = {}) {
  std::printf("{\"ok\":true,\"command\":\"%s\"", json_escape(command).c_str());
  if (!out_path.empty()) {
    std::printf(",\"out\":\"%s\"", json_escape(out_path).c_str());
  }
  std::printf(
      ",\"duration_frames\":%llu,\"stages_ms\":{\"parse\":%.3f,\"structural\":%"
      ".3f,"
      "\"compile\":%.3f,\"graph\":%.3f,\"canonicalize\":%.3f,\"write\":%.3f,"
      "\"cds\":{\"style_resolve\":%.3f,\"shot_planning\":%.3f,"
      "\"transition_planning\":%.3f,\"lint\":%.3f,\"direction_report\":%.3f,"
      "\"total\":%.3f}},\"ir_bytes\":%llu}\n",
      static_cast<unsigned long long>(duration_frames), timing.parse_ms,
      timing.structural_ms, timing.compile_ms, timing.graph_ms,
      timing.canonicalize_ms, timing.write_ms, timing.cds_style_resolve_ms,
      timing.cds_shot_planning_ms, timing.cds_transition_planning_ms,
      timing.cds_lint_ms, timing.cds_direction_report_ms, timing.cds_total_ms,
      static_cast<unsigned long long>(timing.ir_bytes));
}

int cmd_validate(const std::string &path,
                 const std::string &style_bible_path = {}) {
  auto text = read_file(path);
  if (!text) {
    return fail(text.error());
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
  instantee::compiler::CompileOptions options;
  options.style_bible_path = style_bible_path;
  instantee::compiler::CompileStageTiming timing;
  auto compiled = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text->size()), options, &timing);
  if (!compiled) {
    return fail(compiled.error());
  }
  print_ok("validate", timing, compiled->duration_frames.value());
  return 0;
}

int write_text_file(const std::string &path, std::string_view body) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return fail(Error{ExitCode::invalid_input, "io_error", "cli",
                      "Cannot open output file"});
  }
  output << body;
  if (!output) {
    return fail(Error{ExitCode::report_failure, "io_error", "cli",
                      "Cannot write output file"});
  }
  return 0;
}

int cmd_compile(const std::string &path, const std::string &out_path,
                const std::optional<std::string> &direction_report_path,
                const std::string &style_bible_path = {}) {
  auto text = read_file(path);
  if (!text) {
    return fail(text.error());
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
  instantee::compiler::CompileOptions options;
  options.style_bible_path = style_bible_path;
  instantee::compiler::CompileStageTiming timing;
  instantee::direction::DirectionReport report;
  auto compiled = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text->size()), options, &timing,
      direction_report_path ? &report : nullptr);
  if (!compiled) {
    return fail(compiled.error());
  }
  auto json = instantee::compiler::scene_document_to_json(*compiled);
  if (!json) {
    return fail(json.error());
  }
  if (const int wrote = write_text_file(out_path, *json); wrote != 0) {
    return wrote;
  }
  if (direction_report_path) {
    if (const int wrote =
            write_text_file(*direction_report_path, report.to_snapshot());
        wrote != 0) {
      return wrote;
    }
  }
  print_ok("compile", timing, compiled->duration_frames.value(), out_path);
  return 0;
}

int cmd_direction_report(const std::string &path,
                         const std::optional<std::string> &out_path) {
  auto text = read_file(path);
  if (!text) {
    return fail(text.error());
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
  instantee::compiler::CompileOptions options;
  instantee::compiler::CompileStageTiming timing;
  instantee::direction::DirectionReport report;
  auto compiled = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text->size()), options, &timing,
      &report);
  if (!compiled) {
    return fail(compiled.error());
  }
  const std::string snapshot = report.to_snapshot();
  if (out_path) {
    if (const int wrote = write_text_file(*out_path, snapshot); wrote != 0) {
      return wrote;
    }
  }
  if (std::fputs(snapshot.c_str(), stdout) == EOF || std::fflush(stdout) != 0) {
    return fail(Error{ExitCode::report_failure, "stdout_failed", "report",
                      "Cannot write CLI output"});
  }
  return 0;
}

struct FrameArgs {
  std::string scene_path;
  std::uint64_t at = 0;
  std::optional<std::string> out_path;
};

std::expected<FrameArgs, Error>
parse_frame_args(std::span<const std::string_view> args) {
  if (args.empty()) {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments",
                                 "frame requires <scene.json> --at <N>"});
  }
  FrameArgs out;
  out.scene_path = std::string(args.front());
  bool have_at = false;
  for (std::size_t index = 1; index < args.size();) {
    const std::string_view flag = args[index];
    if (flag == "--at") {
      if (have_at) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --at"});
      }
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--at requires a frame index"});
      }
      auto parsed =
          instantee::core::Uint64Encoding::parse_decimal(args[index + 1]);
      if (!parsed) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Invalid --at frame index"});
      }
      out.at = *parsed;
      have_at = true;
      index += 2;
      continue;
    }
    if (flag == "--out") {
      if (out.out_path.has_value()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --out"});
      }
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--out requires a path"});
      }
      out.out_path = std::string(args[index + 1]);
      index += 2;
      continue;
    }
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments", "Unknown frame option"});
  }
  if (!have_at) {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments", "frame requires --at <N>"});
  }
  return out;
}

[[nodiscard]] bool ends_with_png(std::string_view path) noexcept {
  if (path.size() < 4) {
    return false;
  }
  const std::string_view suffix = path.substr(path.size() - 4);
  return suffix == ".png" || suffix == ".PNG";
}

int cmd_frame(const FrameArgs &args) {
  instantee::scene_ir::LoadOptions load_options;
  auto document =
      instantee::scene_ir::load_scene_file(args.scene_path, load_options);
  if (!document) {
    return fail(document.error());
  }
  if (auto graph = instantee::scene_ir::validate_graph(*document); !graph) {
    return fail(graph.error());
  }
  auto scene = instantee::compiler::compile_runtime_scene(*document);
  if (!scene) {
    return fail(scene.error());
  }

  instantee::runtime::AnimationProgram program;
  const bool png_out = args.out_path && ends_with_png(*args.out_path);
  std::expected<instantee::runtime::FramePreview, Error> preview;
  if (png_out) {
    preview = instantee::preview::export_preview_png(
        **scene, program, instantee::core::FrameIndex{args.at}, *args.out_path);
  } else {
    preview = instantee::runtime::preview_frame_at(
        **scene, program, instantee::core::FrameIndex{args.at});
  }
  if (!preview) {
    return fail(preview.error());
  }

  char body[640];
  const int written = std::snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"command\":\"frame\",\"at\":%llu,\"duration_frames\":%llu,"
      "\"seed\":%llu,\"nodes\":%llu,\"evaluations\":%llu,\"color_space\":\"%"
      "s\","
      "\"out_kind\":\"%s\",\"published\":false}",
      static_cast<unsigned long long>(args.at),
      static_cast<unsigned long long>((*scene)->duration_frames().value()),
      static_cast<unsigned long long>((*scene)->seed()),
      static_cast<unsigned long long>(preview->state.nodes.size()),
      static_cast<unsigned long long>(preview->evaluations),
      png_out ? "rec709-sdr" : "logical", png_out ? "png" : "json");
  if (written < 0 || static_cast<std::size_t>(written) >= sizeof(body)) {
    return fail(Error{ExitCode::report_failure, "report_failure", "frame",
                      "Cannot format frame preview report"});
  }

  if (args.out_path && !png_out) {
    std::ofstream output(*args.out_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return fail(Error{ExitCode::invalid_input, "io_error", "cli",
                        "Cannot open output file"});
    }
    output << body << '\n';
    if (!output) {
      return fail(Error{ExitCode::report_failure, "io_error", "cli",
                        "Cannot write output file"});
    }
  }

  if (std::fputs(body, stdout) == EOF || std::fputc('\n', stdout) == EOF ||
      std::fflush(stdout) != 0) {
    return fail(Error{ExitCode::report_failure, "stdout_failed", "report",
                      "Cannot write CLI output"});
  }
  return 0;
}

int cmd_lint(std::span<const std::string_view> args) {
  using instantee::direction::evaluate_lint_policy;
  using instantee::direction::lint_policy_exit_code;
  using instantee::direction::lint_policy_report_json;
  using instantee::direction::lint_release_build_default;
  using instantee::direction::load_lint_findings_file;
  using instantee::direction::load_lint_waivers_file;
  using instantee::direction::parse_lint_cli_options;

  if (args.empty()) {
    usage();
    return static_cast<int>(ExitCode::invalid_input);
  }
  const std::filesystem::path findings_path{std::string{args.front()}};
  auto options =
      parse_lint_cli_options(args.subspan(1), lint_release_build_default());
  if (!options) {
    return fail(options.error());
  }

  auto findings = load_lint_findings_file(findings_path);
  if (!findings) {
    return fail(findings.error());
  }

  std::vector<instantee::direction::LintWaiver> waivers;
  if (options->waivers_path) {
    auto loaded = load_lint_waivers_file(*options->waivers_path);
    if (!loaded) {
      return fail(loaded.error());
    }
    waivers = std::move(*loaded);
  }

  auto report = evaluate_lint_policy(*findings, options->policy, waivers,
                                     lint_release_build_default());
  if (!report) {
    return fail(report.error());
  }

  const std::string body = lint_policy_report_json(*report);
  if (options->report_path) {
    std::ofstream output(*options->report_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
      return fail(Error{ExitCode::invalid_input, "io_error", "cli",
                        "Cannot open lint report output"});
    }
    output << body << '\n';
    if (!output) {
      return fail(Error{ExitCode::report_failure, "io_error", "cli",
                        "Cannot write lint report output"});
    }
  }

  if (std::fputs(body.c_str(), stdout) == EOF ||
      std::fputc('\n', stdout) == EOF || std::fflush(stdout) != 0) {
    return fail(Error{ExitCode::report_failure, "stdout_failed", "report",
                      "Cannot write CLI output"});
  }
  return lint_policy_exit_code(*report);
}

int run(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return static_cast<int>(ExitCode::invalid_input);
  }
  const std::string_view cmd = argv[1];
  if (cmd == "help" || cmd == "--help") {
    usage();
    return 0;
  }
  if (cmd == "validate") {
    if (argc < 3) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::string style_bible;
    if (argc == 5) {
      if (std::string_view(argv[3]) != "--style-bible") {
        usage();
        return static_cast<int>(ExitCode::invalid_input);
      }
      style_bible = argv[4];
    } else if (argc != 3) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_validate(argv[2], style_bible);
  }
  if (cmd == "compile") {
    if (argc < 5 || std::string_view(argv[3]) != "--out") {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::optional<std::string> direction_report;
    std::string style_bible;
    for (int index = 5; index < argc;) {
      const std::string_view flag = argv[index];
      if (flag == "--direction-report") {
        if (direction_report.has_value() || index + 1 >= argc) {
          usage();
          return static_cast<int>(ExitCode::invalid_input);
        }
        direction_report = argv[index + 1];
        index += 2;
        continue;
      }
      if (flag == "--style-bible") {
        if (!style_bible.empty() || index + 1 >= argc) {
          usage();
          return static_cast<int>(ExitCode::invalid_input);
        }
        style_bible = argv[index + 1];
        index += 2;
        continue;
      }
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_compile(argv[2], argv[4], direction_report, style_bible);
  }
  if (cmd == "direction-report") {
    if (argc < 3) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::optional<std::string> out_path;
    if (argc == 5) {
      if (std::string_view(argv[3]) != "--out") {
        usage();
        return static_cast<int>(ExitCode::invalid_input);
      }
      out_path = argv[4];
    } else if (argc != 3) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_direction_report(argv[2], out_path);
  }
  if (cmd == "frame") {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc >= 2 ? argc - 2 : 0));
    for (int index = 2; index < argc; ++index) {
      args.emplace_back(argv[index]);
    }
    auto parsed = parse_frame_args(args);
    if (!parsed) {
      return fail(parsed.error());
    }
    return cmd_frame(*parsed);
  }
  if (cmd == "lint") {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(argc >= 2 ? argc - 2 : 0));
    for (int index = 2; index < argc; ++index) {
      args.emplace_back(argv[index]);
    }
    return cmd_lint(args);
  }
  usage();
  return static_cast<int>(ExitCode::invalid_input);
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (...) {
    return fail(instantee::diagnostics::map_current_exception("cli"));
  }
}
