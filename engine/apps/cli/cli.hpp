#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>
#include <span>
#include <string_view>

namespace instantee::spike {

enum class Command { run, help, smoke };
// Arguments are borrowed only during parsing. Unknown and repeated options
// fail.
[[nodiscard]] std::expected<Command, Error>
parse_command(std::span<const std::string_view> arguments) noexcept;

} // namespace instantee::spike
