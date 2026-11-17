#include <instantee/scene_ir/lockfile.hpp>

#include <instantee/scene_ir/canonicalize.hpp>
#include <instantee/scene_ir/uri_sandbox.hpp>

#include <yyjson.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "asset_lockfile", message};
}

std::string role_for_media(std::string_view media_type) {
  if (media_type.starts_with("font/")) {
    return "font";
  }
  if (media_type.starts_with("image/")) {
    return "image";
  }
  return "other";
}

[[nodiscard]] bool is_hex64(std::string_view text) noexcept {
  if (text.size() != 64U) {
    return false;
  }
  for (char ch : text) {
    const bool ok = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    if (!ok) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *obj, const char *key, std::string_view path) {
  yyjson_val *val = yyjson_obj_get(obj, key);
  if (val == nullptr || !yyjson_is_str(val)) {
    return std::unexpected(make_invalid(
        "missing_field", std::string(path) + "/" + key + " must be a string"));
  }
  return std::string(yyjson_get_str(val));
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

std::expected<AssetLockfile, diagnostics::Error>
parse_asset_lockfile(std::string_view json_text) {
  yyjson_doc *doc =
      yyjson_read(json_text.data(), json_text.size(), YYJSON_READ_NOFLAG);
  if (doc == nullptr) {
    return std::unexpected(
        make_invalid("invalid_json", "Asset lockfile is not valid JSON"));
  }
  struct Guard {
    yyjson_doc *owned = nullptr;
    ~Guard() { yyjson_doc_free(owned); }
  } guard{doc};

  yyjson_val *root = yyjson_doc_get_root(doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(
        make_invalid("invalid_root", "Asset lockfile root must be an object"));
  }

  AssetLockfile out;
  auto schema = require_string(root, "schema_version", "");
  if (!schema) {
    return std::unexpected(schema.error());
  }
  if (*schema != "1.0.0") {
    return std::unexpected(make_invalid(
        "unsupported_schema", "Asset lockfile schema_version must be 1.0.0"));
  }
  out.schema_version = std::move(*schema);

  auto compiler = require_string(root, "compiler_version", "");
  if (!compiler) {
    return std::unexpected(compiler.error());
  }
  out.compiler_version = std::move(*compiler);

  auto profile = require_string(root, "target_feature_profile", "");
  if (!profile) {
    return std::unexpected(profile.error());
  }
  out.target_feature_profile = std::move(*profile);

  auto content = require_string(root, "content_sha256", "");
  if (!content) {
    return std::unexpected(content.error());
  }
  if (!is_hex64(*content)) {
    return std::unexpected(
        make_invalid("invalid_content_sha256",
                     "content_sha256 must be 64 lowercase hex digits"));
  }
  out.content_sha256 = std::move(*content);

  yyjson_val *packages = yyjson_obj_get(root, "style_packages");
  if (packages == nullptr || !yyjson_is_arr(packages)) {
    return std::unexpected(make_invalid("invalid_style_packages",
                                        "style_packages must be an array"));
  }
  yyjson_arr_iter pkg_iter;
  yyjson_arr_iter_init(packages, &pkg_iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&pkg_iter)) {
    if (!yyjson_is_obj(item)) {
      return std::unexpected(make_invalid(
          "invalid_style_package", "style_packages entries must be objects"));
    }
    auto id = require_string(item, "id", "/style_packages");
    auto version = require_string(item, "version", "/style_packages");
    if (!id || !version) {
      return std::unexpected(id ? version.error() : id.error());
    }
    StylePackage pkg;
    pkg.id = std::move(*id);
    pkg.version = std::move(*version);
    out.style_packages.push_back(std::move(pkg));
  }

  yyjson_val *assets = yyjson_obj_get(root, "assets");
  if (assets == nullptr || !yyjson_is_arr(assets)) {
    return std::unexpected(
        make_invalid("invalid_assets", "assets must be an array"));
  }
  if (yyjson_arr_size(assets) == 0) {
    return std::unexpected(
        make_invalid("empty_assets", "assets must be non-empty"));
  }
  yyjson_arr_iter asset_iter;
  yyjson_arr_iter_init(assets, &asset_iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&asset_iter)) {
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_invalid("invalid_asset", "assets entries must be objects"));
    }
    auto id = require_string(item, "id", "/assets");
    if (!id) {
      return std::unexpected(id.error());
    }
    auto uri = require_string(item, "uri", "/assets");
    if (!uri) {
      return std::unexpected(uri.error());
    }
    auto sha = require_string(item, "sha256", "/assets");
    if (!sha) {
      return std::unexpected(sha.error());
    }
    auto media = require_string(item, "media_type", "/assets");
    if (!media) {
      return std::unexpected(media.error());
    }
    auto role = require_string(item, "role", "/assets");
    if (!role) {
      return std::unexpected(role.error());
    }
    if (!is_hex64(*sha)) {
      return std::unexpected(
          make_invalid("invalid_asset_sha256",
                       "asset sha256 must be 64 lowercase hex digits"));
    }
    if (!is_safe_asset_uri_syntax(*uri)) {
      return std::unexpected(
          make_invalid("unsafe_asset_uri", "asset uri failed sandbox syntax"));
    }
    if (*role != "font" && *role != "image" && *role != "other") {
      return std::unexpected(
          make_invalid("invalid_role", "role must be font|image|other"));
    }
    LockfileAsset entry;
    entry.id = std::move(*id);
    entry.uri = std::move(*uri);
    entry.sha256 = std::move(*sha);
    entry.media_type = std::move(*media);
    entry.role = std::move(*role);
    out.assets.push_back(std::move(entry));
  }
  return out;
}

std::expected<AssetLockfile, diagnostics::Error>
load_asset_lockfile_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(
        make_invalid("lockfile_unreadable", "Unable to read asset lockfile"));
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_asset_lockfile(ss.str());
}

std::expected<AssetLockVerification, diagnostics::Error>
verify_asset_lockfile(const AssetLockfile &lockfile,
                      const std::filesystem::path &project_root) {
  if (project_root.empty()) {
    return std::unexpected(make_invalid(
        "missing_project_root", "Project root is required for verification"));
  }
  if (lockfile.assets.empty()) {
    return std::unexpected(
        make_invalid("empty_assets", "Lockfile has no assets to verify"));
  }

  AssetLockVerification result;
  result.offline = true;
  for (const LockfileAsset &asset : lockfile.assets) {
    auto resolved = resolve_asset_uri(project_root, asset.uri);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    std::ifstream in(*resolved, std::ios::binary);
    if (!in) {
      return std::unexpected(
          make_invalid("asset_missing",
                       "Locked asset file is missing offline: " + asset.uri));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string bytes = ss.str();
    const auto *data = reinterpret_cast<const std::byte *>(bytes.data());
    auto digest = sha256_hex(std::span<const std::byte>(data, bytes.size()));
    if (!digest) {
      return std::unexpected(digest.error());
    }
    if (*digest != asset.sha256) {
      return std::unexpected(make_invalid(
          "asset_digest_mismatch",
          "On-disk digest does not match lockfile for " + asset.id));
    }
    ++result.assets_checked;
  }

  AssetLockfile for_digest = lockfile;
  auto finalized = finalize_lockfile(std::move(for_digest));
  if (!finalized) {
    return std::unexpected(finalized.error());
  }
  if (finalized->content_sha256 != lockfile.content_sha256) {
    return std::unexpected(make_invalid(
        "content_digest_mismatch",
        "Lockfile content_sha256 does not match the canonical body"));
  }
  result.content_digest_ok = true;
  return result;
}

} // namespace instantee::scene_ir
