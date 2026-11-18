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
        << "usage: paper_cinema_asset_lock_test <repo-root> [--write-lock]\n";
    return EXIT_FAILURE;
  }
  const std::filesystem::path root{argv[1]};
  const auto lock_path = root / "assets/paper-cinema/paper-cinema.lock.json";
  const auto pin_path = root / "assets/paper-cinema/PIN.json";

  expect(std::filesystem::exists(pin_path), "PIN.json present");
  expect(std::filesystem::exists(lock_path), "lockfile present");

  if (write_lock) {
    auto loaded = load_asset_lockfile_file(lock_path);
    // Placeholder may reject zero digest — build draft from known assets.
    AssetLockfile draft;
    draft.schema_version = "1.0.0";
    draft.compiler_version = "0.1.0";
    draft.target_feature_profile = "metal-apple-baseline";
    draft.style_packages.push_back(
        StylePackage{.id = "paper-cinema", .version = "1.0.0"});
    if (loaded) {
      draft.assets = loaded->assets;
    } else {
      draft.assets = {
          LockfileAsset{.id = "paper-cinema-font-display",
                        .uri = "assets/paper-cinema/fonts/NotoSans-Regular.ttf",
                        .sha256 = "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4"
                                  "925ff43f7bd6a4ebece5",
                        .media_type = "font/ttf",
                        .role = "font"},
          LockfileAsset{
              .id = "paper-cinema-texture-paper-fiber",
              .uri = "assets/paper-cinema/textures/paper-fiber.png",
              .sha256 = "383f7997841ab6af5a3c864400c571814dbc0d90e21a9623b0d4c"
                        "7c9d8ef1985",
              .media_type = "image/png",
              .role = "image"},
          LockfileAsset{
              .id = "paper-cinema-texture-paper-wash",
              .uri = "assets/paper-cinema/textures/paper-wash.png",
              .sha256 = "3abce6e41885ac53192011c339ceb700d0fe46a24ee4944383442"
                        "dbf623f085b",
              .media_type = "image/png",
              .role = "image"},
          LockfileAsset{
              .id = "paper-cinema-pattern-silhouette",
              .uri = "assets/paper-cinema/patterns/silhouette-card.png",
              .sha256 = "d955366e87ae45aba50f899cbcb5dcef917f098b7b0737bc4360"
                        "35637f4eaef0",
              .media_type = "image/png",
              .role = "image"},
      };
    }
    auto finalized = finalize_lockfile(std::move(draft));
    expect(finalized.has_value(), "finalize paper-cinema lock");
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
  expect(lock.has_value(), "load paper-cinema lockfile");
  if (!lock) {
    std::cerr << lock.error().code << ": " << lock.error().message << '\n';
    return EXIT_FAILURE;
  }
  expect(lock->assets.size() >= 4, "font+textures locked");
  expect(!lock->style_packages.empty() &&
             lock->style_packages.front().id == "paper-cinema",
         "paper-cinema style provenance");

  auto verified = verify_asset_lockfile(*lock, root);
  expect(verified.has_value(), "offline asset lock verification");
  if (!verified) {
    std::cerr << verified.error().code << ": " << verified.error().message
              << '\n';
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
