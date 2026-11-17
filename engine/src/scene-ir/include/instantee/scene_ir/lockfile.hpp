#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>

#include <expected>
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

} // namespace instantee::scene_ir
