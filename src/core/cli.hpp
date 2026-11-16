#pragma once

#include <expected>
#include <span>
#include <string_view>

namespace instantee::spike {

enum class Command { run, help, smoke };
enum class ExitCode : int {
  invalid_input = 2,
  gpu_failure = 5,
  export_failure = 6,
  report_failure = 11
};

// Diagnostics contain only static text; they remain usable after allocation
// failure.
struct Error {
  ExitCode exit_code;
  std::string_view code;
  std::string_view phase;
  std::string_view message;
};

// Arguments are borrowed only during parsing. Unknown and repeated options
// fail.
[[nodiscard]] std::expected<Command, Error>
parse_command(std::span<const std::string_view> arguments) noexcept;

} // namespace instantee::spike
