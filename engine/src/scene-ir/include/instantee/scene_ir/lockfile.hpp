#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

struct LockfileAsset {
  std::string id;
  std::string uri;
  std::string sha256;
  std::string media_type;
  std::string role; // e.g. font, image, other
};

struct AssetLockfile {
  std::string schema_version = "1.0.0";
  std::string compiler_version;
  std::string target_feature_profile;
  std::vector<StylePackage> style_packages;
  std::vector<LockfileAsset> assets;
  // SHA-256 of the canonical lockfile body excluding this field.
  std::string content_sha256;
};

[[nodiscard]] AssetLockfile
make_lockfile_from_scene(const SceneDocument &document,
                         std::string_view target_feature_profile);

// Serialize without content_sha256, canonicalize, hash, then fill
// content_sha256.
[[nodiscard]] std::expected<AssetLockfile, diagnostics::Error>
finalize_lockfile(AssetLockfile lockfile);

[[nodiscard]] std::expected<std::string, diagnostics::Error>
lockfile_to_canonical_json(const AssetLockfile &lockfile,
                           bool include_content_hash);

// True when replacing one asset hash changes the lockfile content digest.
[[nodiscard]] bool
lockfile_invalidated_by_asset_change(const AssetLockfile &baseline,
                                     std::string_view asset_id,
                                     std::string_view new_sha256);

// Parse a schema-shaped asset lockfile document (JSON text).
[[nodiscard]] std::expected<AssetLockfile, diagnostics::Error>
parse_asset_lockfile(std::string_view json_text);

[[nodiscard]] std::expected<AssetLockfile, diagnostics::Error>
load_asset_lockfile_file(const std::filesystem::path &path);

struct AssetLockVerification {
  std::size_t assets_checked = 0;
  bool content_digest_ok = false;
  bool offline = true; // verification never opens a network channel
};

// Resolve each lockfile URI under `project_root`, hash file bytes, and confirm
// they match the locked digests. Also re-finalizes the lockfile body and
// compares `content_sha256`. Fails closed on missing files or digest drift.
[[nodiscard]] std::expected<AssetLockVerification, diagnostics::Error>
verify_asset_lockfile(const AssetLockfile &lockfile,
                      const std::filesystem::path &project_root);

} // namespace instantee::scene_ir
