#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/uri_sandbox.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

using instantee::scene_ir::is_safe_asset_uri_syntax;
using instantee::scene_ir::load_scene_json;
using instantee::scene_ir::LoadOptions;
using instantee::scene_ir::resolve_asset_uri;

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_reject_syntax(std::string_view uri, const char *message) {
  expect(!is_safe_asset_uri_syntax(uri), message);
}

[[nodiscard]] std::string minimal_scene_with_uri(std::string_view uri) {
  std::string json;
  json += R"({"schema_version":"1.0.0","compiler_version":"0.1.0",)";
  json += R"("canvas":{"width":8,"height":8,"color":"rec709-sdr"},)";
  json += R"("frame_rate":{"numerator":30,"denominator":1},)";
  json += R"("duration_frames":1,"seed":1,"assets":[{"id":"a0","uri":")";
  json.append(uri);
  json +=
      R"(","sha256":"0000000000000000000000000000000000000000000000000000000000000000",)";
  json += R"("media_type":"font/ttf","color_profile":"untagged"}],)";
  json += R"("materials":[],"geometry":[],"text_runs":[],)";
  json +=
      R"("cameras":[{"id":"cam0","projection":"orthographic","near":0.1,"far":10,)";
  json +=
      R"("transform":{"translate":{"x":0,"y":0,"z":1},"opacity":1}}],"rigs":[],)";
  json +=
      R"("nodes":[{"id":"main_cam","kind":"camera","layer_order":0,"camera_id":"cam0","visible":true}],)";
  json +=
      R"("animation_tracks":[],"shots":[{"id":"s0","start_frame":0,"duration_frames":1,)";
  json +=
      R"("node_ids":["main_cam"],"camera_node_id":"main_cam"}],"transition_spans":[],)";
  json +=
      R"("provenance":{"project_sha256":"0000000000000000000000000000000000000000000000000000000000000000",)";
  json += R"("style_packages":[],"source_schema_version":"1.0.0"}})";
  return json;
}

} // namespace

int main() {
  // --- Syntax negative matrix (no filesystem) ---
  expect(is_safe_asset_uri_syntax("assets/font.ttf"), "accept plain");
  expect(is_safe_asset_uri_syntax("assets/sub/a-b_1.ttf"), "accept nested");

  expect_reject_syntax("", "empty");
  expect_reject_syntax("assets", "missing slash after assets");
  expect_reject_syntax("Assets/font.ttf", "case-sensitive prefix");
  expect_reject_syntax("../assets/font.ttf", "leading parent");
  expect_reject_syntax("assets/../secret", "embedded parent");
  expect_reject_syntax("assets/foo/../../etc/passwd", "deep parent");
  expect_reject_syntax("/etc/passwd", "absolute unix");
  expect_reject_syntax("C:/windows/font.ttf", "drive scheme");
  expect_reject_syntax("assets\\font.ttf", "backslash");
  expect_reject_syntax("assets//x", "double slash");
  expect_reject_syntax("https://example.invalid/x", "https");
  expect_reject_syntax("http://example.invalid/x", "http");
  expect_reject_syntax("file:///etc/passwd", "file scheme");
  expect_reject_syntax("assets/font.ttf?x=1", "query");
  expect_reject_syntax("assets/font.ttf#frag", "fragment");
  {
    const std::string too_long = "assets/" + std::string(600, 'a');
    expect_reject_syntax(too_long, "too long");
  }
  // --- Resolve / symlink matrix ---
  const fs::path root =
      fs::temp_directory_path() / "instantee-uri-sandbox-audit";
  const fs::path outside = fs::temp_directory_path() / "instantee-uri-outside";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::remove_all(outside, ec);
  fs::create_directories(root / "assets" / "sub", ec);
  fs::create_directories(outside, ec);
  std::ofstream(root / "assets" / "ok.txt") << "ok";
  std::ofstream(root / "secret.txt") << "secret";
  std::ofstream(outside / "leak.txt") << "leak";

  {
    auto ok = resolve_asset_uri(root, "assets/ok.txt");
    expect(ok.has_value(), "resolve ok");
    if (ok) {
      expect(ok->filename() == "ok.txt", "resolved name");
    }
  }
  {
    auto nested = resolve_asset_uri(root, "assets/sub/../ok.txt");
    // Syntax rejects `..` before resolve.
    expect(!nested.has_value(), "syntax blocks lexical parent before resolve");
  }

  // Symlink inside assets/ pointing at a file outside the project root.
  {
    const fs::path link = root / "assets" / "escape-link.txt";
    fs::remove(link, ec);
    fs::create_symlink(outside / "leak.txt", link, ec);
    expect(!ec, "create outside symlink");
    auto escaped = resolve_asset_uri(root, "assets/escape-link.txt");
    expect(!escaped.has_value(), "symlink escape outside project rejected");
  }

  // Symlink inside assets/ pointing at a sibling file under project root but
  // outside assets/ — ownership stays in assets sandbox only.
  {
    const fs::path link = root / "assets" / "sibling-link.txt";
    fs::remove(link, ec);
    fs::create_symlink(root / "secret.txt", link, ec);
    expect(!ec, "create sibling symlink");
    auto escaped = resolve_asset_uri(root, "assets/sibling-link.txt");
    expect(!escaped.has_value(), "symlink to non-assets under root rejected");
  }

  // Missing path: weakly_canonical may still succeed; if so it must stay under
  // assets/. Either Error or sandboxed path — never throw / never escape.
  {
    auto missing = resolve_asset_uri(root, "assets/does-not-exist-yet.bin");
    if (missing) {
      const auto rel = missing->lexically_relative(fs::weakly_canonical(root));
      expect(!rel.empty() && *rel.begin() == "assets",
             "missing candidate stays under assets");
    } else {
      expect(missing.error().code == "unsafe_asset_uri",
             "missing path fail-closed code");
    }
  }

  expect(!resolve_asset_uri({}, "assets/ok.txt").has_value(), "empty root");

  // --- Load-time enforcement ---
  {
    LoadOptions options;
    options.project_root = root;
    options.enforce_uri_sandbox = true;
    auto good =
        load_scene_json(minimal_scene_with_uri("assets/ok.txt"), options);
    expect(good.has_value(), "load accepts sandboxed uri");

    auto bad =
        load_scene_json(minimal_scene_with_uri("../assets/ok.txt"), options);
    expect(!bad.has_value(), "load rejects traversal uri");
    if (!bad) {
      expect(bad.error().code == "unsafe_asset_uri", "load error code");
    }

    auto remote = load_scene_json(
        minimal_scene_with_uri("https://example.invalid/x.ttf"), options);
    expect(!remote.has_value(), "load rejects remote uri");
  }

  fs::remove_all(root, ec);
  fs::remove_all(outside, ec);

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
