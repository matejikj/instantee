#include <instantee/scene_ir/lockfile.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::string
pretty_lockfile_json(const instantee::scene_ir::AssetLockfile &lock) {
  // Human-readable committed form (schema order). Digests are authoritative;
  // whitespace is not part of content_sha256 (computed from canonical body).
  std::ostringstream out;
  out << "{\n";
  out << "  \"schema_version\": \"" << lock.schema_version << "\",\n";
  out << "  \"compiler_version\": \"" << lock.compiler_version << "\",\n";
  out << "  \"target_feature_profile\": \"" << lock.target_feature_profile
      << "\",\n";
  out << "  \"style_packages\": [\n";
  for (std::size_t i = 0; i < lock.style_packages.size(); ++i) {
    const auto &pkg = lock.style_packages[i];
    out << "    { \"id\": \"" << pkg.id << "\", \"version\": \"" << pkg.version
        << "\" }";
    if (i + 1 < lock.style_packages.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"assets\": [\n";
  for (std::size_t i = 0; i < lock.assets.size(); ++i) {
    const auto &a = lock.assets[i];
    out << "    {\n";
    out << "      \"id\": \"" << a.id << "\",\n";
    out << "      \"uri\": \"" << a.uri << "\",\n";
    out << "      \"sha256\": \"" << a.sha256 << "\",\n";
    out << "      \"media_type\": \"" << a.media_type << "\",\n";
    out << "      \"role\": \"" << a.role << "\"\n";
    out << "    }";
    if (i + 1 < lock.assets.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ],\n";
  out << "  \"content_sha256\": \"" << lock.content_sha256 << "\"\n";
  out << "}\n";
  return out.str();
}

} // namespace

int main(int argc, char **argv) {
  using namespace instantee::scene_ir;

  const bool write_lock =
      argc == 3 && std::string_view(argv[2]) == "--write-lock";
  if (argc != 2 && !write_lock) {
    std::cerr
        << "usage: editorial_asset_lock_test <repo-root> [--write-lock]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto lock_path = root / "assets/editorial/editorial.lock.json";
  const auto pin_path = root / "assets/editorial/PIN.json";

  expect(std::filesystem::exists(pin_path), "PIN.json present");
  expect(std::filesystem::exists(lock_path), "lockfile present");

  if (write_lock) {
    // Build from PIN-backed files already on disk (placeholder content hash).
    AssetLockfile draft;
    draft.schema_version = "1.0.0";
    draft.compiler_version = "0.1.0";
    draft.target_feature_profile = "metal-apple-baseline";
    draft.style_packages.push_back(
        StylePackage{.id = "editorial", .version = "1.0.0"});

    auto loaded = load_asset_lockfile_file(lock_path);
    // Placeholder lock may have zeros for content_sha256 — parse rejects that
    // only if not hex; zeros are valid hex. Re-read assets from the file via
    // a temporary patch: accept parse then replace digests from disk verify
    // path by reconstructing from known PIN entries through verify inputs.
    if (!loaded) {
      std::cerr << loaded.error().code << ": " << loaded.error().message
                << '\n';
      return EXIT_FAILURE;
    }
    draft.assets = loaded->assets;
    auto finalized = finalize_lockfile(std::move(draft));
    expect(finalized.has_value(), "finalize editorial lock");
    if (!finalized) {
      return EXIT_FAILURE;
    }
    std::ofstream out(lock_path, std::ios::binary | std::ios::trunc);
    out << pretty_lockfile_json(*finalized);
    expect(static_cast<bool>(out), "write lockfile");
    std::cout << "wrote " << lock_path.string()
              << " content_sha256=" << finalized->content_sha256 << '\n';
  }

  auto lock = load_asset_lockfile_file(lock_path);
  expect(lock.has_value(), "load editorial lockfile");
  if (!lock) {
    std::cerr << lock.error().code << ": " << lock.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(lock->assets.size() >= 5, "fonts+texture+photos locked");
  expect(!lock->style_packages.empty(), "style package provenance");

  auto verified = verify_asset_lockfile(*lock, root);
  expect(verified.has_value(), "offline asset lock verification");
  if (!verified) {
    std::cerr << verified.error().code << ": " << verified.error().message
              << '\n';
    return EXIT_FAILURE;
  }
  expect(verified->offline, "verification stays offline");
  expect(verified->content_digest_ok, "content digest ok");
  expect(verified->assets_checked == lock->assets.size(),
         "every asset checked");

  // Traceability: each URI resolves under assets/ and is readable.
  for (const auto &asset : lock->assets) {
    expect(asset.uri.starts_with("assets/editorial/"),
           "editorial uri namespace");
    expect(asset.sha256.size() == 64, "asset digest length");
  }

  // Digest drift is detected (reproducible lock).
  AssetLockfile mutated = *lock;
  mutated.assets.front().sha256 =
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
  expect(!verify_asset_lockfile(mutated, root),
         "reject lockfile with drifted asset digest");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return EXIT_FAILURE;
  }
  std::cout << "editorial_asset_lock_test: ok\n";
  return EXIT_SUCCESS;
}
