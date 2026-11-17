#include <instantee/core_model/integer_encoding.hpp>

#include <limits>

namespace instantee::core {
namespace {

diagnostics::Error invalid_seed(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input,
                            "invalid_uint64_encoding", "contracts", message};
}

} // namespace

std::expected<std::uint64_t, diagnostics::Error>
Uint64Encoding::parse_decimal(std::string_view text) noexcept {
  if (text.empty()) {
    return std::unexpected(invalid_seed("Empty uint64 decimal string"));
  }
  if (text.size() > 20U) {
    return std::unexpected(invalid_seed("uint64 decimal string is too long"));
  }
  if (text.size() > 1U && text.front() == '0') {
    return std::unexpected(
        invalid_seed("uint64 decimal string has a leading zero"));
  }
  std::uint64_t value = 0;
  constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return std::unexpected(
          invalid_seed("uint64 decimal string contains a non-digit"));
    }
    const std::uint64_t digit =
        static_cast<std::uint64_t>(static_cast<unsigned char>(ch - '0'));
    if (value > kMax / 10U) {
      return std::unexpected(invalid_seed("uint64 decimal string overflows"));
    }
    value *= 10U;
    if (value > kMax - digit) {
      return std::unexpected(invalid_seed("uint64 decimal string overflows"));
    }
    value += digit;
  }
  return value;
}

std::string Uint64Encoding::format_decimal(std::uint64_t value) {
  if (value == 0U) {
    return "0";
  }
  char buffer[20];
  std::size_t index = sizeof(buffer);
  while (value != 0U) {
    buffer[--index] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  }
  return std::string(buffer + index, sizeof(buffer) - index);
}

std::expected<std::uint64_t, diagnostics::Error>
Uint64Encoding::from_json_number(std::uint64_t value) noexcept {
  if (value > kJsSafeIntegerMax) {
    return std::unexpected(invalid_seed(
        "JSON number form exceeds the JS safe integer maximum; use a decimal "
        "string"));
  }
  return value;
}

std::expected<std::uint64_t, diagnostics::Error>
Uint64Encoding::from_json_string(std::string_view text) noexcept {
  return parse_decimal(text);
}

Uint64JsonForm Uint64Encoding::preferred_form(std::uint64_t value) noexcept {
  return value <= kJsSafeIntegerMax ? Uint64JsonForm::number
                                    : Uint64JsonForm::decimal_string;
}

} // namespace instantee::core
