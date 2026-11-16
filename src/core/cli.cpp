#include "cli.hpp"

namespace instantee::spike {

std::expected<Command, Error>
parse_command(std::span<const std::string_view> arguments) noexcept {
  if (arguments.empty()) {
    return Command::run;
  }
  if (arguments.size() == 1) {
    if (arguments.front() == "--help") {
      return Command::help;
    }
    if (arguments.front() == "--smoke") {
      return Command::smoke;
    }
  }
  return std::unexpected(Error{ExitCode::invalid_input, "invalid_arguments",
                               "arguments", "Expected --help or --smoke"});
}

} // namespace instantee::spike
