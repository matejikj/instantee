#include <instantee/scene_ir/release_lock.hpp>

#include <instantee/scene_ir/canonicalize.hpp>

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "release_asset_lock", message};
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

bool is_hex64(std::string_view text) {
  if (text.size() != 64U) {
    return false;
  }
  return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
    return std::isxdigit(ch) != 0 &&
           (std::isdigit(ch) != 0 || (ch >= 'a' && ch <= 'f'));
  });
}

bool role_allowed(std::string_view role) {
  return role == "font" || role == "texture" || role == "blueprint" ||
         role == "style_bible" || role == "style_lock" || role == "license" ||
         role == "pin";
}

std::expected<std::string, diagnostics::Error>
require_string(yyjson_val *obj, const char *key, std::string_view path) {
  yyjson_val *value = yyjson_obj_get(obj, key);
  if (value == nullptr || !yyjson_is_str(value)) {
    return std::unexpected(make_invalid(
        "missing_field", std::string(path) + ": missing string " + key));
  }
  return std::string(yyjson_get_str(value), yyjson_get_len(value));
}

std::expected<std::string, diagnostics::Error>
hash_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(
        make_invalid("asset_missing", "Missing locked file: " + path.string()));
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string bytes = ss.str();
  const auto *data = reinterpret_cast<const std::byte *>(bytes.data());
  return sha256_hex(std::span<const std::byte>(data, bytes.size()));
}

} // namespace

bool is_safe_repo_relative_path(std::string_view path) noexcept {
  if (path.empty() || path.size() > 512U) {
    return false;
  }
  if (path.front() == '/' || path.contains('\\') || path.contains(':') ||
      path.contains("//") || path.contains("..")) {
    return false;
  }
  for (const char ch : path) {
    const auto uch = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '.' || ch == '_' || ch == '-' ||
        std::isalnum(uch) != 0) {
      continue;
    }
    return false;
  }
  return true;
}

std::expected<std::filesystem::path, diagnostics::Error>
resolve_repo_relative_path(const std::filesystem::path &repo_root,
                           std::string_view relative) {
  if (repo_root.empty()) {
    return std::unexpected(
        make_invalid("missing_repo_root", "Repository root is required"));
  }
  if (!is_safe_repo_relative_path(relative)) {
    return std::unexpected(make_invalid(
        "unsafe_repo_path", "Release lock path failed sandbox checks"));
  }
  std::error_code ec;
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(repo_root, ec);
  if (ec) {
    return std::unexpected(
        make_invalid("bad_repo_root", "Cannot canonicalize repository root"));
  }
  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(root / std::string(relative), ec);
  if (ec) {
    return std::unexpected(
        make_invalid("bad_repo_path", "Cannot canonicalize release lock path"));
  }
  const std::string root_text = root.generic_string();
  const std::string candidate_text = candidate.generic_string();
  if (candidate_text != root_text &&
      !candidate_text.starts_with(root_text + "/")) {
    return std::unexpected(
        make_invalid("path_escape", "Release lock path escapes repository"));
  }
  return candidate;
}

std::expected<std::string, diagnostics::Error>
release_lock_to_canonical_json(const ReleaseAssetLock &lock,
                               bool include_content_hash) {
  std::ostringstream json;
  json << '{';
  json << "\"build_flags\":[";
  for (std::size_t i = 0; i < lock.build_flags.size(); ++i) {
    if (i != 0U) {
      json << ',';
    }
    const auto &flag = lock.build_flags[i];
    json << "{\"key\":\"" << flag.key << "\",\"value\":\"" << flag.value
         << "\"}";
  }
  json << "],";
  json << "\"compiler_version\":\"" << lock.compiler_version << "\",";
  if (include_content_hash) {
    json << "\"content_sha256\":\"" << lock.content_sha256 << "\",";
  }
  json << "\"entries\":[";
  for (std::size_t i = 0; i < lock.entries.size(); ++i) {
    if (i != 0U) {
      json << ',';
    }
    const auto &entry = lock.entries[i];
    json << '{' << "\"id\":\"" << entry.id << "\","
         << "\"license\":\"" << entry.license << "\",";
    if (!entry.license_path.empty()) {
      json << "\"license_path\":\"" << entry.license_path << "\",";
    }
    json << "\"path\":\"" << entry.path << "\","
         << "\"role\":\"" << entry.role << "\","
         << "\"sha256\":\"" << entry.sha256 << "\"}";
  }
  json << "],";
  json << "\"lock_id\":\"" << lock.lock_id << "\",";
  json << "\"schema_version\":\"" << lock.schema_version << "\",";
  json << "\"style_packages\":[";
  for (std::size_t i = 0; i < lock.style_packages.size(); ++i) {
    if (i != 0U) {
      json << ',';
    }
    const auto &pkg = lock.style_packages[i];
    json << "{\"id\":\"" << pkg.id << "\",\"version\":\"" << pkg.version
         << "\"}";
  }
  json << "],";
  json << "\"target_feature_profile\":\"" << lock.target_feature_profile
       << "\"";
  json << '}';
  return canonicalize_json(json.str());
}

std::expected<ReleaseAssetLock, diagnostics::Error>
finalize_release_lock(ReleaseAssetLock lock) {
  std::sort(lock.build_flags.begin(), lock.build_flags.end(),
            [](const ReleaseLockBuildFlag &a, const ReleaseLockBuildFlag &b) {
              return a.key < b.key;
            });
  std::sort(
      lock.style_packages.begin(), lock.style_packages.end(),
      [](const StylePackage &a, const StylePackage &b) { return a.id < b.id; });
  std::sort(lock.entries.begin(), lock.entries.end(),
            [](const ReleaseLockEntry &a, const ReleaseLockEntry &b) {
              return a.id < b.id;
            });
  auto body = release_lock_to_canonical_json(lock, false);
  if (!body) {
    return std::unexpected(body.error());
  }
  const auto *data = reinterpret_cast<const std::byte *>(body->data());
  auto digest = sha256_hex(std::span<const std::byte>(data, body->size()));
  if (!digest) {
    return std::unexpected(digest.error());
  }
  lock.content_sha256 = *digest;
  return lock;
}

std::expected<ReleaseAssetLock, diagnostics::Error>
parse_release_asset_lock(std::string_view json_text) {
  if (json_text.empty()) {
    return std::unexpected(
        make_invalid("invalid_json", "Release lock JSON is empty"));
  }
  std::string buffer(json_text);
  yyjson_read_err read_err{};
  DocGuard guard{
      yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &read_err)};
  if (guard.doc == nullptr) {
    return std::unexpected(
        make_invalid("invalid_json", "Release lock is not valid JSON"));
  }
  yyjson_val *root = yyjson_doc_get_root(guard.doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(
        make_invalid("invalid_root", "Release lock root must be an object"));
  }

  ReleaseAssetLock out;
  auto schema = require_string(root, "schema_version", "/");
  if (!schema) {
    return std::unexpected(schema.error());
  }
  if (*schema != "1.0.0") {
    return std::unexpected(diagnostics::Error{
        diagnostics::ExitCode::incompatible_schema,
        diagnostics::ErrorKind::incompatible_schema, "unsupported_schema",
        "release_asset_lock", "Unsupported release lock schema_version"}
                               .with_json_path("/schema_version"));
  }
  out.schema_version = *schema;

  auto lock_id = require_string(root, "lock_id", "/");
  auto compiler = require_string(root, "compiler_version", "/");
  auto profile = require_string(root, "target_feature_profile", "/");
  auto content = require_string(root, "content_sha256", "/");
  if (!lock_id || !compiler || !profile || !content) {
    return std::unexpected(lock_id ? compiler ? profile ? content.error()
                                                        : profile.error()
                                              : compiler.error()
                                   : lock_id.error());
  }
  if (!is_hex64(*content)) {
    return std::unexpected(
        make_invalid("invalid_content_sha256",
                     "content_sha256 must be 64 lowercase hex digits"));
  }
  out.lock_id = std::move(*lock_id);
  out.compiler_version = std::move(*compiler);
  out.target_feature_profile = std::move(*profile);
  out.content_sha256 = std::move(*content);

  yyjson_val *flags = yyjson_obj_get(root, "build_flags");
  if (flags == nullptr || !yyjson_is_arr(flags)) {
    return std::unexpected(
        make_invalid("invalid_build_flags", "build_flags must be an array"));
  }
  yyjson_arr_iter flag_iter;
  yyjson_arr_iter_init(flags, &flag_iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&flag_iter)) {
    if (!yyjson_is_obj(item)) {
      return std::unexpected(make_invalid(
          "invalid_build_flag", "build_flags entries must be objects"));
    }
    auto key = require_string(item, "key", "/build_flags");
    auto value = require_string(item, "value", "/build_flags");
    if (!key || !value) {
      return std::unexpected(key ? value.error() : key.error());
    }
    out.build_flags.push_back(
        ReleaseLockBuildFlag{std::move(*key), std::move(*value)});
  }

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

  yyjson_val *entries = yyjson_obj_get(root, "entries");
  if (entries == nullptr || !yyjson_is_arr(entries) ||
      yyjson_arr_size(entries) == 0) {
    return std::unexpected(
        make_invalid("invalid_entries", "entries must be a non-empty array"));
  }
  yyjson_arr_iter entry_iter;
  yyjson_arr_iter_init(entries, &entry_iter);
  while (yyjson_val *item = yyjson_arr_iter_next(&entry_iter)) {
    if (!yyjson_is_obj(item)) {
      return std::unexpected(
          make_invalid("invalid_entry", "entries must be objects"));
    }
    auto id = require_string(item, "id", "/entries");
    auto path = require_string(item, "path", "/entries");
    auto sha = require_string(item, "sha256", "/entries");
    auto role = require_string(item, "role", "/entries");
    auto license = require_string(item, "license", "/entries");
    if (!id || !path || !sha || !role || !license) {
      return std::unexpected(
          id ? path ? sha ? role ? license.error() : role.error() : sha.error()
                    : path.error()
             : id.error());
    }
    if (!is_hex64(*sha)) {
      return std::unexpected(
          make_invalid("invalid_entry_sha256",
                       "entry sha256 must be 64 lowercase hex digits"));
    }
    if (!is_safe_repo_relative_path(*path)) {
      return std::unexpected(
          make_invalid("unsafe_repo_path", "entry path failed sandbox checks"));
    }
    if (!role_allowed(*role)) {
      return std::unexpected(make_invalid(
          "invalid_role",
          "role must be "
          "font|texture|blueprint|style_bible|style_lock|license|pin"));
    }
    ReleaseLockEntry entry;
    entry.id = std::move(*id);
    entry.path = std::move(*path);
    entry.sha256 = std::move(*sha);
    entry.role = std::move(*role);
    entry.license = std::move(*license);
    if (yyjson_val *license_path = yyjson_obj_get(item, "license_path");
        license_path != nullptr) {
      if (!yyjson_is_str(license_path)) {
        return std::unexpected(make_invalid("invalid_license_path",
                                            "license_path must be a string"));
      }
      entry.license_path = std::string(yyjson_get_str(license_path),
                                       yyjson_get_len(license_path));
      if (!is_safe_repo_relative_path(entry.license_path)) {
        return std::unexpected(make_invalid(
            "unsafe_repo_path", "license_path failed sandbox checks"));
      }
    }
    out.entries.push_back(std::move(entry));
  }
  return out;
}

std::expected<ReleaseAssetLock, diagnostics::Error>
load_release_asset_lock_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::unexpected(
        make_invalid("lockfile_unreadable", "Unable to read release lock"));
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_release_asset_lock(ss.str());
}

std::expected<ReleaseLockVerification, diagnostics::Error>
verify_release_asset_lock(const ReleaseAssetLock &lock,
                          const std::filesystem::path &repo_root) {
  if (repo_root.empty()) {
    return std::unexpected(
        make_invalid("missing_repo_root", "Repository root is required"));
  }
  if (lock.entries.empty()) {
    return std::unexpected(
        make_invalid("empty_entries", "Release lock has no entries"));
  }

  ReleaseLockVerification result;
  result.offline = true;
  for (const ReleaseLockEntry &entry : lock.entries) {
    auto resolved = resolve_repo_relative_path(repo_root, entry.path);
    if (!resolved) {
      return std::unexpected(resolved.error());
    }
    auto digest = hash_file(*resolved);
    if (!digest) {
      return std::unexpected(digest.error());
    }
    if (*digest != entry.sha256) {
      return std::unexpected(make_invalid(
          "asset_digest_mismatch",
          "On-disk digest does not match release lock for " + entry.id));
    }
    ++result.entries_checked;
    if (!entry.license_path.empty()) {
      auto license = resolve_repo_relative_path(repo_root, entry.license_path);
      if (!license) {
        return std::unexpected(license.error());
      }
      if (!std::filesystem::exists(*license)) {
        return std::unexpected(make_invalid(
            "license_missing", "License file missing for " + entry.id + ": " +
                                   entry.license_path));
      }
      ++result.licenses_checked;
    }
  }

  ReleaseAssetLock for_digest = lock;
  auto finalized = finalize_release_lock(std::move(for_digest));
  if (!finalized) {
    return std::unexpected(finalized.error());
  }
  if (finalized->content_sha256 != lock.content_sha256) {
    return std::unexpected(make_invalid(
        "content_digest_mismatch",
        "Release lock content_sha256 does not match the canonical body"));
  }
  result.content_digest_ok = true;
  return result;
}

bool release_lock_detects_modified_entry(const ReleaseAssetLock &baseline,
                                         std::string_view entry_id,
                                         std::string_view new_sha256) {
  ReleaseAssetLock mutated = baseline;
  bool found = false;
  for (ReleaseLockEntry &entry : mutated.entries) {
    if (entry.id == entry_id) {
      entry.sha256 = std::string(new_sha256);
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  auto before = finalize_release_lock(baseline);
  auto after = finalize_release_lock(std::move(mutated));
  if (!before || !after) {
    return false;
  }
  return before->content_sha256 != after->content_sha256;
}

} // namespace instantee::scene_ir
