#include "contract.hpp"

#include <expected>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202211L
#error "std::expected with monadic operations is required"
#endif

int main() {
  const std::expected<int, int> value{21};
  const std::expected<int, int> error{std::unexpected{7}};
  const auto doubled = value.transform([](int input) { return input * 2; });
  return doubled && *doubled == 42 && !error && error.error() == 7 ? 0 : 1;
}
