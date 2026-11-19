#include <instantee/compiler/compile.hpp>
#include <instantee/compiler/compile_runtime.hpp>
#include <instantee/core_model/integer_encoding.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/diagnostics/exception_boundary.hpp>
#include <instantee/direction/lint_policy.hpp>
#include <instantee/export/scene_video_export.hpp>
#include <instantee/export_apple/apple_video_sink.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/preview/frame_png.hpp>
#include <instantee/runtime/frame_preview.hpp>
#include <instantee/runtime/render_job.hpp>
#include <instantee/scene_ir/graph_validate.hpp>
#include <instantee/scene_ir/load.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
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
using instantee::export_apple::AppleVideoSink;
using instantee::export_core::DurabilityState;
using instantee::export_core::PublishMode;
using instantee::runtime::AnimationProgram;
using instantee::runtime::RenderJob;
using instantee::video_export::export_scene_video;
using instantee::video_export::SceneVideoExportRequest;

struct CliFlags {
  bool json_log = false;
};

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

[[nodiscard]] const char *durability_label(DurabilityState state,
                                           bool published) noexcept {
  if (!published) {
    return "not_published";
  }
  switch (state) {
  case DurabilityState::confirmed:
    return "confirmed";
  case DurabilityState::unknown:
    return "unknown";
  case DurabilityState::not_applicable:
    return "not_applicable";
  }
  return "not_published";
}

int fail(const Error &error, bool published = false,
         DurabilityState durability = DurabilityState::not_applicable) {
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
  std::fprintf(stderr, ",\"published\":%s,\"durability\":\"%s\"}\n",
               published ? "true" : "false",
               durability_label(durability, published));
  return static_cast<int>(error.exit_code);
}

void emit_log(const CliFlags &flags, std::string_view event,
              std::string_view command, std::string_view body_fields) {
  if (!flags.json_log) {
    return;
  }
  std::fprintf(stderr,
               "{\"event\":\"%s\",\"command\":\"%s\"%s,\"published\":false}\n",
               json_escape(event).c_str(), json_escape(command).c_str(),
               body_fields.empty() ? "" : std::string(body_fields).c_str());
}

void usage() {
  std::fputs(
      "usage:\n"
      "  instantee [--json-log] validate <authoring.json> "
      "[--style-bible <bible.json>]\n"
      "  instantee [--json-log] compile <authoring.json> --out <scene.json> "
      "[--direction-report <report.txt>] [--style-bible <bible.json>]\n"
      "  instantee [--json-log] direction-report <authoring.json> "
      "[--out <report.txt>]\n"
      "  instantee [--json-log] render <scene.json> --out <video.mp4> "
      "[--report <report.json>] [--frames <N>] [--overwrite] [--progress]\n"
      "  instantee [--json-log] frame <scene.json> --at <N> [--out "
      "<report.json|preview.png>]\n"
      "  instantee [--json-log] benchmark <scene.json|authoring.json> "
      "--preset m1-air [--out <report.json>] [--frames <N>]\n"
      "  instantee [--json-log] lint <findings.json> "
      "[--policy off|warn|strict] [--waivers <waivers.json>] "
      "[--report <report.json>]\n"
      "  instantee help\n"
      "\n"
      "Global --json-log emits NDJSON diagnostics on stderr (including "
      "progress when requested). Exit codes are stable process boundaries; "
      "errors always include code/phase/published/durability on stderr.\n"
      "Lint policy defaults to strict in release builds and warn otherwise. "
      "Strict rejects unwaived severity=error findings. Optional Style Bible "
      "path loads tokens during CDS style resolve and timed lint.\n"
      "benchmark timings are scriptable harness output and are not a "
      "certified M1 Air performance claim.\n",
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

int cmd_validate(const CliFlags &flags, const std::string &path,
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
  emit_log(flags, "ok", "validate", "");
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

int cmd_compile(const CliFlags &flags, const std::string &path,
                const std::string &out_path,
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
  emit_log(flags, "ok", "compile",
           ",\"out\":\"" + json_escape(out_path) + "\"");
  return 0;
}

int cmd_direction_report(const CliFlags &flags, const std::string &path,
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
  emit_log(flags, "ok", "direction-report", "");
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

int cmd_frame(const CliFlags &flags, const FrameArgs &args) {
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

  AnimationProgram program;
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
      "\"out_kind\":\"%s\",\"published\":false,\"durability\":\"not_"
      "published\"}",
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
  emit_log(flags, "ok", "frame", "");
  return 0;
}

struct RenderArgs {
  std::string scene_path;
  std::string out_path;
  std::optional<std::string> report_path;
  std::optional<std::uint64_t> frame_limit;
  bool overwrite = false;
  bool progress = false;
};

std::expected<RenderArgs, Error>
parse_render_args(std::span<const std::string_view> args) {
  if (args.empty()) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "invalid_arguments", "arguments",
              "render requires <scene.json> --out <video.mp4>"});
  }
  RenderArgs out;
  out.scene_path = std::string(args.front());
  bool have_out = false;
  for (std::size_t index = 1; index < args.size();) {
    const std::string_view flag = args[index];
    if (flag == "--out") {
      if (have_out) {
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
      have_out = true;
      index += 2;
      continue;
    }
    if (flag == "--report") {
      if (out.report_path.has_value()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --report"});
      }
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--report requires a path"});
      }
      out.report_path = std::string(args[index + 1]);
      index += 2;
      continue;
    }
    if (flag == "--frames") {
      if (out.frame_limit.has_value()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --frames"});
      }
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--frames requires a count"});
      }
      auto parsed =
          instantee::core::Uint64Encoding::parse_decimal(args[index + 1]);
      if (!parsed || *parsed == 0) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Invalid --frames count"});
      }
      out.frame_limit = *parsed;
      index += 2;
      continue;
    }
    if (flag == "--overwrite") {
      if (out.overwrite) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --overwrite"});
      }
      out.overwrite = true;
      ++index;
      continue;
    }
    if (flag == "--progress") {
      if (out.progress) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --progress"});
      }
      out.progress = true;
      ++index;
      continue;
    }
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments", "Unknown render option"});
  }
  if (!have_out || out.out_path.empty()) {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments", "render requires --out <path>"});
  }
  return out;
}

void emit_progress(const CliFlags &flags, bool enabled, std::uint64_t completed,
                   std::uint64_t total) {
  if (!enabled && !flags.json_log) {
    return;
  }
  if (flags.json_log || enabled) {
    std::fprintf(
        stderr,
        "{\"event\":\"progress\",\"command\":\"render\",\"frame\":%llu,"
        "\"total\":%llu,\"published\":false,\"durability\":\"not_"
        "published\"}\n",
        static_cast<unsigned long long>(completed),
        static_cast<unsigned long long>(total));
    return;
  }
}

int cmd_render(const CliFlags &flags, const RenderArgs &args) {
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

  RenderJob job(std::move(*scene));
  AnimationProgram program;
  SceneVideoExportRequest request;
  request.target_path = args.out_path;
  request.frame_count_limit = args.frame_limit;
  request.publish_mode =
      args.overwrite ? PublishMode::overwrite : PublishMode::no_replace;
  request.durability = instantee::export_core::DurabilityClass::durable_local;
  const bool want_progress = args.progress || flags.json_log;
  if (want_progress) {
    request.on_progress = [&flags, progress = args.progress](
                              std::uint64_t completed, std::uint64_t total) {
      emit_progress(flags, progress, completed, total);
    };
  }

  auto factory = [](const std::filesystem::path &staging)
      -> std::expected<std::unique_ptr<instantee::export_core::VideoSink>,
                       Error> {
    auto sink = AppleVideoSink::create(staging);
    if (!sink) {
      return std::unexpected(sink.error());
    }
    return std::unique_ptr<instantee::export_core::VideoSink>(std::move(*sink));
  };

  auto result = export_scene_video(job, program, request, factory);
  if (!result) {
    return fail(result.error());
  }

  const char *dur = durability_label(result->durability, result->published);
  const std::string phase =
      std::string(instantee::runtime::render_job_phase_id(result->phase));
  char body[768];
  const int written = std::snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"command\":\"render\",\"out\":\"%s\",\"frames_appended\":%"
      "llu,\"phase\":\"%s\",\"published\":%s,\"durability\":\"%s\"}",
      json_escape(args.out_path).c_str(),
      static_cast<unsigned long long>(result->frames_appended),
      json_escape(phase).c_str(), result->published ? "true" : "false", dur);
  if (written < 0 || static_cast<std::size_t>(written) >= sizeof(body)) {
    return fail(Error{ExitCode::report_failure, "report_failure", "render",
                      "Cannot format render report"},
                result->published, result->durability);
  }

  if (args.report_path) {
    if (const int wrote =
            write_text_file(*args.report_path, std::string(body) + "\n");
        wrote != 0) {
      // Target may already be published; keep durability truthful.
      return fail(Error{ExitCode::report_failure, "io_error", "render",
                        "Cannot write render report"},
                  result->published, result->durability);
    }
  }

  if (result->post_publish_error) {
    std::fprintf(stderr, "%s\n", body);
    return fail(*result->post_publish_error, result->published,
                result->durability);
  }

  if (std::fputs(body, stdout) == EOF || std::fputc('\n', stdout) == EOF ||
      std::fflush(stdout) != 0) {
    return fail(Error{ExitCode::report_failure, "stdout_failed", "report",
                      "Cannot write CLI output"},
                result->published, result->durability);
  }
  emit_log(flags, "ok", "render",
           ",\"out\":\"" + json_escape(args.out_path) + "\"");
  return 0;
}

struct BenchmarkArgs {
  std::string input_path;
  std::string preset;
  std::optional<std::string> out_path;
  std::uint64_t frames = 1;
};

std::expected<BenchmarkArgs, Error>
parse_benchmark_args(std::span<const std::string_view> args) {
  if (args.empty()) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "invalid_arguments", "arguments",
              "benchmark requires <input.json> --preset m1-air"});
  }
  BenchmarkArgs out;
  out.input_path = std::string(args.front());
  bool have_preset = false;
  for (std::size_t index = 1; index < args.size();) {
    const std::string_view flag = args[index];
    if (flag == "--preset") {
      if (have_preset) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Repeated --preset"});
      }
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--preset requires a name"});
      }
      out.preset = std::string(args[index + 1]);
      have_preset = true;
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
    if (flag == "--frames") {
      if (index + 1 >= args.size()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "--frames requires a count"});
      }
      auto parsed =
          instantee::core::Uint64Encoding::parse_decimal(args[index + 1]);
      if (!parsed || *parsed == 0) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "invalid_arguments", "arguments",
                                     "Invalid --frames count"});
      }
      out.frames = *parsed;
      index += 2;
      continue;
    }
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments", "Unknown benchmark option"});
  }
  if (!have_preset) {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments",
                                 "benchmark requires --preset m1-air"});
  }
  if (out.preset != "m1-air") {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                                 "arguments",
                                 "Only --preset m1-air is supported"});
  }
  return out;
}

[[nodiscard]] double ms_since(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - start)
      .count();
}

int cmd_benchmark(const CliFlags &flags, const BenchmarkArgs &args) {
  using clock = std::chrono::steady_clock;
  const auto wall_start = clock::now();

  std::unique_ptr<const instantee::runtime::CompiledScene> scene;
  std::string input_kind = "scene";
  double load_ms = 0.0;
  double compile_ms = 0.0;
  instantee::compiler::CompileStageTiming authoring_timing{};

  {
    const auto t0 = clock::now();
    instantee::scene_ir::LoadOptions load_options;
    auto document =
        instantee::scene_ir::load_scene_file(args.input_path, load_options);
    load_ms = ms_since(t0);
    if (document) {
      if (auto graph = instantee::scene_ir::validate_graph(*document); !graph) {
        return fail(graph.error());
      }
      const auto t1 = clock::now();
      auto compiled = instantee::compiler::compile_runtime_scene(*document);
      compile_ms = ms_since(t1);
      if (!compiled) {
        return fail(compiled.error());
      }
      scene = std::move(*compiled);
    } else {
      auto text = read_file(args.input_path);
      if (!text) {
        return fail(document.error());
      }
      input_kind = "authoring";
      const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
      instantee::compiler::CompileOptions options;
      const auto t1 = clock::now();
      auto compiled = instantee::compiler::compile_authoring_json(
          std::span<const std::byte>(bytes, text->size()), options,
          &authoring_timing);
      compile_ms = ms_since(t1);
      if (!compiled) {
        // Prefer authoring error when scene load failed on the same path.
        return fail(compiled.error());
      }
      auto runtime = instantee::compiler::compile_runtime_scene(*compiled);
      if (!runtime) {
        return fail(runtime.error());
      }
      scene = std::move(*runtime);
      load_ms = authoring_timing.parse_ms + authoring_timing.structural_ms;
    }
  }

  AnimationProgram program;
  const std::uint64_t duration = scene->duration_frames().value();
  const std::uint64_t limit =
      std::min(args.frames, duration == 0 ? 1 : duration);
  const auto eval_start = clock::now();
  std::uint64_t evaluations = 0;
  for (std::uint64_t frame = 0; frame < limit; ++frame) {
    auto preview = instantee::runtime::preview_frame_at(
        *scene, program, instantee::core::FrameIndex{frame});
    if (!preview) {
      return fail(preview.error());
    }
    evaluations += preview->evaluations;
  }
  const double eval_ms = ms_since(eval_start);
  const double wall_ms = ms_since(wall_start);

  char body[1024];
  const int written = std::snprintf(
      body, sizeof(body),
      "{\"ok\":true,\"command\":\"benchmark\",\"preset\":\"%s\",\"input_kind\":"
      "\"%s\",\"certified\":false,\"frames\":%llu,\"duration_frames\":%llu,"
      "\"evaluations\":%llu,\"timings_ms\":{\"load\":%.3f,\"compile\":%.3f,"
      "\"evaluate\":%.3f,\"wall\":%.3f},\"published\":false,\"durability\":"
      "\"not_published\"}",
      json_escape(args.preset).c_str(), input_kind.c_str(),
      static_cast<unsigned long long>(limit),
      static_cast<unsigned long long>(duration),
      static_cast<unsigned long long>(evaluations), load_ms, compile_ms,
      eval_ms, wall_ms);
  if (written < 0 || static_cast<std::size_t>(written) >= sizeof(body)) {
    return fail(Error{ExitCode::report_failure, "report_failure", "benchmark",
                      "Cannot format benchmark report"});
  }

  if (args.out_path) {
    if (const int wrote =
            write_text_file(*args.out_path, std::string(body) + "\n");
        wrote != 0) {
      return wrote;
    }
  }

  if (std::fputs(body, stdout) == EOF || std::fputc('\n', stdout) == EOF ||
      std::fflush(stdout) != 0) {
    return fail(Error{ExitCode::report_failure, "stdout_failed", "report",
                      "Cannot write CLI output"});
  }
  emit_log(flags, "ok", "benchmark",
           ",\"certified\":false,\"preset\":\"" + json_escape(args.preset) +
               "\"");
  return 0;
}

int cmd_lint(const CliFlags &flags, std::span<const std::string_view> args) {
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
  emit_log(flags, "ok", "lint", "");
  return lint_policy_exit_code(*report);
}

[[nodiscard]] std::vector<std::string_view> collect_args(int argc, char **argv,
                                                         int start) {
  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc >= start ? argc - start : 0));
  for (int index = start; index < argc; ++index) {
    args.emplace_back(argv[index]);
  }
  return args;
}

int run(int argc, char **argv) {
  CliFlags flags;
  int index = 1;
  while (index < argc) {
    const std::string_view arg = argv[index];
    if (arg == "--json-log") {
      if (flags.json_log) {
        usage();
        return static_cast<int>(ExitCode::invalid_input);
      }
      flags.json_log = true;
      ++index;
      continue;
    }
    break;
  }

  if (index >= argc) {
    usage();
    return static_cast<int>(ExitCode::invalid_input);
  }

  const std::string_view cmd = argv[index];
  if (cmd == "help" || cmd == "--help") {
    usage();
    return 0;
  }

  if (cmd == "validate") {
    if (argc < index + 2) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::string style_bible;
    if (argc == index + 4) {
      if (std::string_view(argv[index + 2]) != "--style-bible") {
        usage();
        return static_cast<int>(ExitCode::invalid_input);
      }
      style_bible = argv[index + 3];
    } else if (argc != index + 2) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_validate(flags, argv[index + 1], style_bible);
  }

  if (cmd == "compile") {
    if (argc < index + 4 || std::string_view(argv[index + 2]) != "--out") {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::optional<std::string> direction_report;
    std::string style_bible;
    for (int cursor = index + 4; cursor < argc;) {
      const std::string_view flag = argv[cursor];
      if (flag == "--direction-report") {
        if (direction_report.has_value() || cursor + 1 >= argc) {
          usage();
          return static_cast<int>(ExitCode::invalid_input);
        }
        direction_report = argv[cursor + 1];
        cursor += 2;
        continue;
      }
      if (flag == "--style-bible") {
        if (!style_bible.empty() || cursor + 1 >= argc) {
          usage();
          return static_cast<int>(ExitCode::invalid_input);
        }
        style_bible = argv[cursor + 1];
        cursor += 2;
        continue;
      }
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_compile(flags, argv[index + 1], argv[index + 3],
                       direction_report, style_bible);
  }

  if (cmd == "direction-report") {
    if (argc < index + 2) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    std::optional<std::string> out_path;
    if (argc == index + 4) {
      if (std::string_view(argv[index + 2]) != "--out") {
        usage();
        return static_cast<int>(ExitCode::invalid_input);
      }
      out_path = argv[index + 3];
    } else if (argc != index + 2) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_direction_report(flags, argv[index + 1], out_path);
  }

  if (cmd == "render") {
    auto parsed = parse_render_args(collect_args(argc, argv, index + 1));
    if (!parsed) {
      return fail(parsed.error());
    }
    return cmd_render(flags, *parsed);
  }

  if (cmd == "frame") {
    auto parsed = parse_frame_args(collect_args(argc, argv, index + 1));
    if (!parsed) {
      return fail(parsed.error());
    }
    return cmd_frame(flags, *parsed);
  }

  if (cmd == "benchmark") {
    auto parsed = parse_benchmark_args(collect_args(argc, argv, index + 1));
    if (!parsed) {
      return fail(parsed.error());
    }
    return cmd_benchmark(flags, *parsed);
  }

  if (cmd == "lint") {
    return cmd_lint(flags, collect_args(argc, argv, index + 1));
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
