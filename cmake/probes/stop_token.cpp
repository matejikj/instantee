#include "contract.hpp"

#include <stop_token>

int main() {
  std::stop_source source;
  const auto token = source.get_token();
  int calls = 0;
  std::stop_callback callback{token, [&calls]() noexcept { ++calls; }};
  if (!token.stop_possible() || token.stop_requested() ||
      !source.request_stop() || source.request_stop()) {
    return 1;
  }
  return token.stop_requested() && calls == 1 ? 0 : 2;
}
