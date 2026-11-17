#include <instantee/compiler/compile.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/diagnostics/exception_boundary.hpp>

#include <cstdio>
#include <expected>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

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
  std::fputs("usage:\n"
             "  instantee validate <authoring.json>\n"
             "  instantee compile <authoring.json> --out <scene.json>\n"
             "  instantee help\n",
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
      "\"compile\":%.3f,\"graph\":%.3f,\"canonicalize\":%.3f,\"write\":%.3f},"
      "\"ir_bytes\":%llu}\n",
      static_cast<unsigned long long>(duration_frames), timing.parse_ms,
      timing.structural_ms, timing.compile_ms, timing.graph_ms,
      timing.canonicalize_ms, timing.write_ms,
      static_cast<unsigned long long>(timing.ir_bytes));
}

int cmd_validate(const std::string &path) {
  auto text = read_file(path);
  if (!text) {
    return fail(text.error());
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
  instantee::compiler::CompileOptions options;
  instantee::compiler::CompileStageTiming timing;
  auto compiled = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text->size()), options, &timing);
  if (!compiled) {
    return fail(compiled.error());
  }
  print_ok("validate", timing, compiled->duration_frames.value());
  return 0;
}

int cmd_compile(const std::string &path, const std::string &out_path) {
  auto text = read_file(path);
  if (!text) {
    return fail(text.error());
  }
  const auto *bytes = reinterpret_cast<const std::byte *>(text->data());
  instantee::compiler::CompileOptions options;
  instantee::compiler::CompileStageTiming timing;
  auto compiled = instantee::compiler::compile_authoring_json(
      std::span<const std::byte>(bytes, text->size()), options, &timing);
  if (!compiled) {
    return fail(compiled.error());
  }
  auto json = instantee::compiler::scene_document_to_json(*compiled);
  if (!json) {
    return fail(json.error());
  }
  std::ofstream output(out_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return fail(Error{ExitCode::invalid_input, "io_error", "cli",
                      "Cannot open output file"});
  }
  output << *json;
  if (!output) {
    return fail(Error{ExitCode::report_failure, "io_error", "cli",
                      "Cannot write output file"});
  }
  print_ok("compile", timing, compiled->duration_frames.value(), out_path);
  return 0;
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
    if (argc != 3) {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_validate(argv[2]);
  }
  if (cmd == "compile") {
    if (argc != 5 || std::string_view(argv[3]) != "--out") {
      usage();
      return static_cast<int>(ExitCode::invalid_input);
    }
    return cmd_compile(argv[2], argv[4]);
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
