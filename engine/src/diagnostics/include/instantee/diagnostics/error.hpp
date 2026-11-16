#pragma once

#include <string_view>

namespace instantee::spike {

enum class ExitCode : int {
  invalid_input = 2,
  gpu_failure = 5,
  export_failure = 6,
  publication_conflict = 9,
  published_durability_unknown = 10,
  report_failure = 11,
  deadline_exceeded = 12
};

// Diagnostics contain only static text; they remain usable after allocation
// failure.
struct Error {
  ExitCode exit_code;
  std::string_view code;
  std::string_view phase;
  std::string_view message;
};

} // namespace instantee::spike
