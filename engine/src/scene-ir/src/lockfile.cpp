#include <instantee/scene_ir/lockfile.hpp>

#include <instantee/scene_ir/canonicalize.hpp>

#include <algorithm>
#include <sstream>

namespace instantee::scene_ir {
namespace {

std::string role_for_media(std::string_view media_type) {
  if (media_type.starts_with("font/")) {
    return "font";
  }
  if (media_type.starts_with("image/")) {
    return "image";
  }
  return "other";
}

} // namespace

AssetLockfile
make_lockfile_from_scene(const SceneDocument &document,
                         std::string_view target_feature_profile) {
  AssetLockfile lockfile;
  lockfile.compiler_version = document.compiler_version;
  lockfile.target_feature_profile = std::string(target_feature_profile);
  lockfile.style_packages = document.provenance.style_packages;
  lockfile.assets.reserve(document.assets.size());
  for (const Asset &asset : document.assets) {
    LockfileAsset entry;
    entry.id = asset.id;
    entry.uri = asset.uri;
    entry.sha256 = asset.sha256;
    entry.media_type = asset.media_type;
    entry.role = role_for_media(asset.media_type);
    lockfile.assets.push_back(std::move(entry));
  }
  std::sort(lockfile.assets.begin(), lockfile.assets.end(),
            [](const LockfileAsset &lhs, const LockfileAsset &rhs) {
              return lhs.id < rhs.id;
            });
  return lockfile;
}

std::expected<std::string, diagnostics::Error>
lockfile_to_canonical_json(const AssetLockfile &lockfile,
                           bool include_content_hash) {
  std::ostringstream json;
  json << '{';
  json << "\"assets\":[";
  for (std::size_t index = 0; index < lockfile.assets.size(); ++index) {
    if (index != 0U) {
      json << ',';
    }
    const LockfileAsset &asset = lockfile.assets[index];
    json << '{' << "\"id\":\"" << asset.id << "\","
         << "\"media_type\":\"" << asset.media_type << "\","
         << "\"role\":\"" << asset.role << "\","
         << "\"sha256\":\"" << asset.sha256 << "\","
         << "\"uri\":\"" << asset.uri << "\"" << '}';
  }
  json << "],";
  json << "\"compiler_version\":\"" << lockfile.compiler_version << "\",";
  if (include_content_hash) {
    json << "\"content_sha256\":\"" << lockfile.content_sha256 << "\",";
  }
  json << "\"schema_version\":\"" << lockfile.schema_version << "\",";
  json << "\"style_packages\":[";
  for (std::size_t index = 0; index < lockfile.style_packages.size(); ++index) {
    if (index != 0U) {
      json << ',';
    }
    const StylePackage &pkg = lockfile.style_packages[index];
    json << '{' << "\"id\":\"" << pkg.id << "\","
         << "\"version\":\"" << pkg.version << "\"" << '}';
  }
  json << "],";
  json << "\"target_feature_profile\":\"" << lockfile.target_feature_profile
       << "\"";
  json << '}';
  return canonicalize_json(json.str());
}

std::expected<AssetLockfile, diagnostics::Error>
finalize_lockfile(AssetLockfile lockfile) {
  auto body = lockfile_to_canonical_json(lockfile, false);
  if (!body) {
    return std::unexpected(body.error());
  }
  const auto *data = reinterpret_cast<const std::byte *>(body->data());
  auto digest = sha256_hex(std::span<const std::byte>(data, body->size()));
  if (!digest) {
    return std::unexpected(digest.error());
  }
  lockfile.content_sha256 = *digest;
  return lockfile;
}

bool lockfile_invalidated_by_asset_change(const AssetLockfile &baseline,
                                          std::string_view asset_id,
                                          std::string_view new_sha256) {
  AssetLockfile mutated = baseline;
  for (LockfileAsset &asset : mutated.assets) {
    if (asset.id == asset_id) {
      asset.sha256 = std::string(new_sha256);
    }
  }
  auto before = finalize_lockfile(baseline);
  auto after = finalize_lockfile(mutated);
  if (!before || !after) {
    return true;
  }
  return before->content_sha256 != after->content_sha256;
}

} // namespace instantee::scene_ir
