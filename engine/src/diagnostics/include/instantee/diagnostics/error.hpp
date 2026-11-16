#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace instantee::diagnostics {

// Process-level exit mapping used by CLI entry points. Domain code prefers
// ErrorKind; ExitCode remains the stable numeric boundary for the process.
enum class ExitCode : int {
  invalid_input = 2,
  gpu_failure = 5,
  export_failure = 6,
  publication_conflict = 9,
  published_durability_unknown = 10,
  report_failure = 11,
  deadline_exceeded = 12,
  cancelled = 13,
  closed = 14,
  would_block = 15,
  resource_budget_exceeded = 16,
  out_of_memory = 17,
  unexpected_exception = 18
};

// Typed taxonomy for expected failures. Distinct kinds stay distinct even when
// several historically shared ExitCode::invalid_input.
enum class ErrorKind : std::uint16_t {
  invalid_input = 1,
  cancelled,
  closed,
  would_block,
  deadline_exceeded,
  resource_budget_exceeded,
  gpu_failure,
  export_failure,
  publication_conflict,
  published_durability_unknown,
  report_failure,
  out_of_memory,
  unexpected_exception
};

// Owned dynamic context. Strings are owned by Error so views into caller
// temporaries cannot outlive the diagnostic.
struct ErrorContext {
  std::optional<std::string> json_path;
  std::optional<std::uint64_t> frame;
  std::optional<std::string> shot;
  std::optional<std::string> node;
  std::optional<int> system_code;
};

// Production error value for std::expected. Code/phase/message are owned so a
// diagnostic remains valid after the creating stack frame returns. Context
// fields replace only when explicitly set; existing text is never truncated.
struct Error {
  ExitCode exit_code = ExitCode::invalid_input;
  ErrorKind kind = ErrorKind::invalid_input;
  std::string code;
  std::string phase;
  std::string message;
  ErrorContext context{};

  Error() = default;

  Error(ExitCode exit_code, std::string_view code, std::string_view phase,
        std::string_view message);

  Error(ExitCode exit_code, ErrorKind kind, std::string_view code,
        std::string_view phase, std::string_view message);

  Error(ExitCode exit_code, ErrorKind kind, std::string_view code,
        std::string_view phase, std::string_view message, ErrorContext context);

  [[nodiscard]] Error with_json_path(std::string path) const &;
  [[nodiscard]] Error with_json_path(std::string path) &&;
  [[nodiscard]] Error with_frame(std::uint64_t frame) const &;
  [[nodiscard]] Error with_frame(std::uint64_t frame) &&;
  [[nodiscard]] Error with_shot(std::string shot) const &;
  [[nodiscard]] Error with_shot(std::string shot) &&;
  [[nodiscard]] Error with_node(std::string node) const &;
  [[nodiscard]] Error with_node(std::string node) &&;
  [[nodiscard]] Error with_system_code(int system_code) const &;
  [[nodiscard]] Error with_system_code(int system_code) &&;

  // Stable typed single-line snapshot for contract tests. Field order is
  // fixed; absent context fields are omitted rather than padded or truncated.
  [[nodiscard]] std::string snapshot() const;

  [[nodiscard]] static ErrorKind kind_for(ExitCode exit_code) noexcept;
  [[nodiscard]] static ExitCode exit_code_for(ErrorKind kind) noexcept;
  [[nodiscard]] static ErrorKind kind_for_code(std::string_view code) noexcept;
};

} // namespace instantee::diagnostics
