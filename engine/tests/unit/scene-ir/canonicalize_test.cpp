#include <instantee/scene_ir/canonicalize.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using instantee::scene_ir::canonical_sha256_hex;
  using instantee::scene_ir::canonicalize_json;

  const auto a = canonicalize_json(R"({"b":1,"a":2})");
  const auto b = canonicalize_json(R"({"a":2,"b":1})");
  expect(a.has_value() && b.has_value(), "canonicalize both");
  if (a && b) {
    expect(*a == *b, "key order independent");
    expect(*a == R"({"a":2,"b":1})", "sorted keys snapshot");
  }

  const auto ha = canonical_sha256_hex(R"({"z":true,"a":[3,1]})");
  const auto hb = canonical_sha256_hex(R"({"a":[3,1],"z":true})");
  expect(ha.has_value() && hb.has_value() && *ha == *hb, "hash equality");

  const auto seed_a = canonicalize_json(R"({"seed":"18446744073709551615"})");
  const auto seed_b = canonicalize_json(R"({"seed":"18446744073709551615"})");
  expect(seed_a && seed_b && *seed_a == *seed_b, "uint64 string preserved");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
