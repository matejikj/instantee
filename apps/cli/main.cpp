#include "cli.hpp"
#include "workspace.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <new>
#include <span>
#include <string_view>

namespace {
using namespace instantee::spike;

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

int run(int argc, char **argv) {
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
                    "--smoke checks the native workspace without export or "
                    "performance measurement.\n",
                    tool));
  case Command::smoke:
    if (auto result = apple::check_workspace(); !result) {
      return fail(result.error());
    }
    return finish_stdout(
        std::puts("{\"workspace\":\"ok\",\"published\":false,\"certified\":"
                  "false}"));
  case Command::run:
    return fail(unavailable);
  }
  return static_cast<int>(ExitCode::invalid_input);
}
} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
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
