#include <instantee/scene_ir/canonicalize.hpp>
#include <instantee/scene_ir/load.hpp>
#include <instantee/scene_ir/release_lock.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::string read_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void write_pretty_lock(const std::filesystem::path &path,
                       const instantee::scene_ir::ReleaseAssetLock &lock) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << "{\n";
  out << "  \"schema_version\": \"" << lock.schema_version << "\",\n";
  out << "  \"lock_id\": \"" << lock.lock_id << "\",\n";
  out << "  \"compiler_version\": \"" << lock.compiler_version << "\",\n";
  out << "  \"target_feature_profile\": \"" << lock.target_feature_profile
      << "\",\n";
  out << "  \"build_flags\": [\n";
  for (std::size_t i = 0; i < lock.build_flags.size(); ++i) {
    out << "    { \"key\": \"" << lock.build_flags[i].key << "\", \"value\": \""
        << lock.build_flags[i].value << "\" }";
    out << (i + 1 < lock.build_flags.size() ? ",\n" : "\n");
  }
  out << "  ],\n";
  out << "  \"style_packages\": [\n";
  for (std::size_t i = 0; i < lock.style_packages.size(); ++i) {
    out << "    { \"id\": \"" << lock.style_packages[i].id
        << "\", \"version\": \"" << lock.style_packages[i].version << "\" }";
    out << (i + 1 < lock.style_packages.size() ? ",\n" : "\n");
  }
  out << "  ],\n";
  out << "  \"entries\": [\n";
  for (std::size_t i = 0; i < lock.entries.size(); ++i) {
    const auto &entry = lock.entries[i];
    out << "    {\n";
    out << "      \"id\": \"" << entry.id << "\",\n";
    out << "      \"path\": \"" << entry.path << "\",\n";
    out << "      \"sha256\": \"" << entry.sha256 << "\",\n";
    out << "      \"role\": \"" << entry.role << "\",\n";
    out << "      \"license\": \"" << entry.license << "\"";
    if (!entry.license_path.empty()) {
      out << ",\n      \"license_path\": \"" << entry.license_path << "\"\n";
    } else {
      out << "\n";
    }
    out << "    }";
    out << (i + 1 < lock.entries.size() ? ",\n" : "\n");
  }
  out << "  ],\n";
  out << "  \"content_sha256\": \"" << lock.content_sha256 << "\"\n";
  out << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path root = INSTANTEE_SOURCE_DIR;
  bool write_lock = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--write-lock") {
      write_lock = true;
    }
  }

  const auto lock_path = root / "contracts/release/release-asset-lock-v1.json";
  auto loaded = instantee::scene_ir::load_release_asset_lock_file(lock_path);
  expect(loaded.has_value(), "load release lock");
  if (!loaded) {
    std::cerr << loaded.error().message << '\n';
    return EXIT_FAILURE;
  }

  auto finalized = instantee::scene_ir::finalize_release_lock(*loaded);
  expect(finalized.has_value(), "finalize release lock");
  if (!finalized) {
    return EXIT_FAILURE;
  }

  if (write_lock) {
    write_pretty_lock(lock_path, *finalized);
    std::cout << "wrote " << lock_path
              << " content_sha256=" << finalized->content_sha256 << '\n';
    return EXIT_SUCCESS;
  }

  expect(finalized->content_sha256 == loaded->content_sha256,
         "content digest matches finalized body");
  expect(!finalized->build_flags.empty(), "build flags recorded");
  expect(finalized->style_packages.size() == 3U, "three style packages");
  expect(finalized->entries.size() >= 10U, "release entries present");

  auto verified = instantee::scene_ir::verify_release_asset_lock(*loaded, root);
  expect(verified.has_value(), "offline release lock verifies");
  if (verified) {
    expect(verified->offline, "verification stays offline");
    expect(verified->content_digest_ok, "content digest ok");
    expect(verified->entries_checked == loaded->entries.size(),
           "all entries checked");
    expect(verified->licenses_checked > 0U, "license files checked");
  }

  expect(
      instantee::scene_ir::release_lock_detects_modified_entry(
          *loaded, loaded->entries.front().id,
          "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
      "modified asset changes release lock digest");

  // Identical IR from a clean offline load of a locked scene fixture.
  instantee::scene_ir::LoadOptions options;
  const auto scene_text = read_file(
      root / "contracts/fixtures/scene-ir/positive/minimal.scene.json");
  auto first = instantee::scene_ir::load_scene_json(scene_text, options);
  auto second = instantee::scene_ir::load_scene_json(scene_text, options);
  expect(first.has_value() && second.has_value(), "load locked scene twice");
  if (first && second) {
    expect(first->schema_version == second->schema_version, "schema stable");
    expect(first->seed == second->seed, "seed stable");
    expect(first->duration_frames == second->duration_frames,
           "duration stable");
    auto a = instantee::scene_ir::canonical_sha256_hex(scene_text);
    auto b = instantee::scene_ir::canonical_sha256_hex(scene_text);
    expect(a.has_value() && b.has_value() && *a == *b,
           "identical IR canonical digest offline");
  }

  // Detect on-disk mismatch: verify against a poisoned expected hash.
  auto poisoned = *loaded;
  poisoned.entries.front().sha256 =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  auto mismatch =
      instantee::scene_ir::verify_release_asset_lock(poisoned, root);
  expect(!mismatch.has_value(), "modified asset digest is detected");
  if (!mismatch) {
    expect(mismatch.error().code == "asset_digest_mismatch" ||
               mismatch.error().code == "content_digest_mismatch",
           "digest mismatch error code");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "release asset lock reproducibility checks passed\n";
  return EXIT_SUCCESS;
}
