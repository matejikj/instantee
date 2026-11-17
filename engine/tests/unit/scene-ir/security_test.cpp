#include <instantee/scene_ir/limits.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/uri_sandbox.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  using instantee::scene_ir::InputLimits;
  using instantee::scene_ir::is_safe_asset_uri_syntax;
  using instantee::scene_ir::load_scene_json;
  using instantee::scene_ir::LoadOptions;
  using instantee::scene_ir::resolve_asset_uri;

  expect(is_safe_asset_uri_syntax("assets/font.ttf"), "accept relative asset");
  expect(!is_safe_asset_uri_syntax("../assets/font.ttf"),
         "reject parent escape");
  expect(!is_safe_asset_uri_syntax("/etc/passwd"), "reject absolute");
  expect(!is_safe_asset_uri_syntax("assets/../secret"),
         "reject dotdot segment");
  expect(!is_safe_asset_uri_syntax("https://example.invalid/x"),
         "reject scheme");
  expect(!is_safe_asset_uri_syntax("assets//x"), "reject double slash");

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "instantee-uri-sandbox";
  std::filesystem::create_directories(root / "assets");
  std::ofstream(root / "assets" / "ok.txt") << "ok";

  const auto ok = resolve_asset_uri(root, "assets/ok.txt");
  expect(ok.has_value(), "resolve sandboxed asset");

  const auto escape = resolve_asset_uri(root, "assets/../ok.txt");
  expect(!escape.has_value(), "reject path traversal uri");

  LoadOptions options;
  options.limits.max_bytes = 64;
  const auto oversized = load_scene_json(std::string(128, 'x'), options);
  expect(!oversized.has_value(), "reject oversized input");
  if (!oversized) {
    expect(oversized.error().code == "limit_exceeded", "limit_exceeded code");
  }

  options.limits = InputLimits{};
  const auto invalid_depth =
      load_scene_json(std::string(80, '[') + std::string(80, ']'), options);
  expect(!invalid_depth.has_value(), "reject deep nesting / invalid root");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
