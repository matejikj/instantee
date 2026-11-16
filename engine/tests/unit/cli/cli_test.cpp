#include "cli.hpp"

#include <array>
#include <string_view>

int main() {
  using namespace instantee::spike;
  using instantee::diagnostics::ExitCode;
  using namespace std::literals;
  if (parse_command({}) != Command::run ||
      parse_command(std::array{"--help"sv}) != Command::help ||
      parse_command(std::array{"--smoke"sv}) != Command::smoke) {
    return 1;
  }
  for (auto input :
       {""sv, "--unknown"sv, "--help=1"sv, "file.mp4"sv, "--SMOKE"sv}) {
    const auto result = parse_command(std::array{input});
    if (result || result.error().exit_code != ExitCode::invalid_input ||
        result.error().phase != "arguments") {
      return 2;
    }
  }
  for (auto inputs : {std::array{"--smoke"sv, "--smoke"sv},
                      std::array{"--help"sv, "--smoke"sv},
                      std::array{"--help"sv, "file.mp4"sv}}) {
    if (parse_command(inputs)) {
      return 3;
    }
  }
}
