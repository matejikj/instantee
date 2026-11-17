#include <instantee/core_model/integer_encoding.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void usage() {
  std::cerr << "usage: contract-seed-check parse-decimal <text>\n"
            << "       contract-seed-check from-number <uint>\n"
            << "       contract-seed-check preferred-form <uint>\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    usage();
    return 2;
  }
  const std::string_view command = argv[1];
  const std::string_view argument = argv[2];
  using instantee::core::Uint64Encoding;
  using instantee::core::Uint64JsonForm;

  if (command == "parse-decimal") {
    const auto value = Uint64Encoding::from_json_string(argument);
    if (!value) {
      std::cerr << value.error().snapshot() << '\n';
      return 2;
    }
    std::cout << Uint64Encoding::format_decimal(*value) << '\n';
    return 0;
  }

  if (command == "from-number" || command == "preferred-form") {
    const auto parsed = Uint64Encoding::parse_decimal(argument);
    if (!parsed) {
      std::cerr << parsed.error().snapshot() << '\n';
      return 2;
    }
    if (command == "from-number") {
      const auto value = Uint64Encoding::from_json_number(*parsed);
      if (!value) {
        std::cerr << value.error().snapshot() << '\n';
        return 2;
      }
      std::cout << Uint64Encoding::format_decimal(*value) << '\n';
      return 0;
    }
    const auto form = Uint64Encoding::preferred_form(*parsed);
    std::cout << (form == Uint64JsonForm::number ? "number" : "decimal_string")
              << '\n';
    return 0;
  }

  usage();
  return 2;
}
