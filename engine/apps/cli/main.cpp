#include "cli.hpp"
#include <instantee/export_apple/workspace.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/runtime/stop_signals.hpp>

#include <array>
#include <cstdio>
#include <exception>
#include <new>
#include <span>
#include <string_view>

namespace {
using namespace instantee::spike;
using instantee::diagnostics::Error;
using instantee::diagnostics::ExitCode;

#if defined(INSTANTEE_SPIKE_BENCHMARK)
constexpr auto tool = "spike-benchmark";
constexpr Error unavailable{ExitCode::invalid_input, "benchmark_unavailable",
                            "benchmark",
                            "Export measurement is not implemented"};
#else
constexpr auto tool = "spike-render";
constexpr Error unavailable{ExitCode::gpu_failure, "renderer_unavailable",
                            "render", "Metal rendering is not implemented"};
#endif

// Only static project text reaches this allocation-free diagnostic boundary.
int fail(const Error &error) noexcept {
  std::fprintf(stderr,
               "{\"code\":\"%.*s\",\"phase\":\"%.*s\",\"message\":\"%.*s\","
               "\"published\":false,\"durability\":\"not_published\"}\n",
               static_cast<int>(error.code.size()), error.code.data(),
               static_cast<int>(error.phase.size()), error.phase.data(),
               static_cast<int>(error.message.size()), error.message.data());
  return static_cast<int>(error.exit_code);
}

int finish_stdout(int result) noexcept {
  if (result < 0 || std::fflush(stdout) != 0) {
    return fail({ExitCode::report_failure, "stdout_failed", "report",
                 "Cannot write CLI output"});
  }
  return 0;
}

[[nodiscard]] bool write_json_string(std::FILE *out,
                                     std::string_view value) noexcept {
  if (std::fputc('"', out) == EOF) {
    return false;
  }
  for (unsigned char ch : value) {
    if (ch < 0x20 || ch == '"' || ch == '\\' || ch > 0x7E) {
      if (std::fprintf(out, "\\u%04x", ch) < 0) {
        return false;
      }
      continue;
    }
    if (std::fputc(ch, out) == EOF) {
      return false;
    }
  }
  return std::fputc('"', out) != EOF;
}

[[nodiscard]] int
write_smoke_report(const metal::DeviceReport &metal) noexcept {
  if (std::fputs("{\"workspace\":\"ok\",\"metal\":{\"name\":", stdout) == EOF ||
      !write_json_string(stdout, metal.name) ||
      std::fputs(",\"architecture\":", stdout) == EOF ||
      !write_json_string(stdout, metal.architecture) ||
      std::fprintf(
          stdout,
          ",\"recommended_max_working_set_bytes\":%llu,"
          "\"max_buffer_length_bytes\":%llu,\"has_unified_memory\":%s,"
          "\"is_low_power\":%s,\"is_headless\":%s,\"supports_apple7\":%s,"
          "\"supports_metal3\":%s,\"has_command_queue\":%s},"
          "\"published\":false,\"certified\":false}\n",
          static_cast<unsigned long long>(
              metal.recommended_max_working_set_bytes),
          static_cast<unsigned long long>(metal.max_buffer_length_bytes),
          metal.has_unified_memory ? "true" : "false",
          metal.is_low_power ? "true" : "false",
          metal.is_headless ? "true" : "false",
          metal.supports_apple7 ? "true" : "false",
          metal.supports_metal3 ? "true" : "false",
          metal.has_command_queue ? "true" : "false") < 0) {
    return -1;
  }
  return 0;
}

int run(int argc, char **argv, std::stop_token stop) {
  // The parser accepts at most one option. Bound storage even for hostile argv.
  std::array<std::string_view, 2> arguments{};
  const auto count = argc > 2 ? 2U : (argc > 1 ? 1U : 0U);
  for (unsigned i = 0; i < count; ++i) {
    arguments[i] = argv[i + 1];
  }
  const auto command = parse_command(std::span{arguments}.first(count));
  if (!command) {
    return fail(command.error());
  }
  switch (*command) {
  case Command::help:
    return finish_stdout(
        std::printf("Usage: %s [--help | --smoke]\n"
                    "--smoke checks the native workspace and Metal device "
                    "without export or performance measurement.\n",
                    tool));
  case Command::smoke: {
    if (stop.stop_requested()) {
      return fail({ExitCode::invalid_input, "cancelled", "cli",
                   "Native smoke check was cancelled"});
    }
    if (auto result = apple::check_workspace(); !result) {
      return fail(result.error());
    }
    auto device = metal::Device::open();
    if (!device) {
      return fail(device.error());
    }
    if (stop.stop_requested()) {
      return fail({ExitCode::invalid_input, "cancelled", "cli",
                   "Native smoke check was cancelled"});
    }
    return finish_stdout(write_smoke_report(device->report()));
  }
  case Command::run:
    return fail(unavailable);
  }
  return static_cast<int>(ExitCode::invalid_input);
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::stop_source stop;
    auto signals = instantee::runtime::StopSignals::start(stop);
    if (!signals) {
      return fail(signals.error());
    }
    return run(argc, argv, stop.get_token());
  } catch (const std::bad_alloc &) {
    return fail({ExitCode::export_failure, "out_of_memory", "cli",
                 "Allocation failed"});
  } catch (const std::exception &) {
    return fail({ExitCode::export_failure, "unexpected_exception", "cli",
                 "Unexpected library exception"});
  } catch (...) {
    return fail({ExitCode::export_failure, "unexpected_exception", "cli",
                 "Unexpected exception"});
  }
}
