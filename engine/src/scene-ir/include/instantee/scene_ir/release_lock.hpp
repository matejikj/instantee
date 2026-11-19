#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/document.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

// Release-wide lock for fonts, textures, blueprints, Style Bible documents,
// nested style locks, licenses and recorded build flags. Paths are repository
// relative (not limited to assets/). Verification is offline-only.

struct ReleaseLockBuildFlag {
  std::string key;
  std::string value;
};

struct ReleaseLockEntry {
  std::string id;
  std::string path; // repo-relative, no '..'
  std::string sha256;
  std::string role; // font|texture|blueprint|style_bible|style_lock|license|pin
  std::string license;      // SPDX-ish short id or license label
  std::string license_path; // optional repo-relative license file
};

struct ReleaseAssetLock {
  std::string schema_version = "1.0.0";
  std::string lock_id;
  std::string compiler_version;
  std::string target_feature_profile;
  std::vector<ReleaseLockBuildFlag> build_flags;
  std::vector<StylePackage> style_packages;
  std::vector<ReleaseLockEntry> entries;
  std::string content_sha256;
};

struct ReleaseLockVerification {
  std::size_t entries_checked = 0;
  std::size_t licenses_checked = 0;
  bool content_digest_ok = false;
  bool offline = true;
};

[[nodiscard]] bool is_safe_repo_relative_path(std::string_view path) noexcept;

[[nodiscard]] std::expected<std::filesystem::path, diagnostics::Error>
resolve_repo_relative_path(const std::filesystem::path &repo_root,
                           std::string_view relative);

[[nodiscard]] std::expected<std::string, diagnostics::Error>
release_lock_to_canonical_json(const ReleaseAssetLock &lock,
                               bool include_content_hash);

[[nodiscard]] std::expected<ReleaseAssetLock, diagnostics::Error>
finalize_release_lock(ReleaseAssetLock lock);

[[nodiscard]] std::expected<ReleaseAssetLock, diagnostics::Error>
parse_release_asset_lock(std::string_view json_text);

[[nodiscard]] std::expected<ReleaseAssetLock, diagnostics::Error>
load_release_asset_lock_file(const std::filesystem::path &path);

// Hash every entry (and optional license file) under repo_root. Never opens a
// network channel. Fails closed on missing files or digest drift.
[[nodiscard]] std::expected<ReleaseLockVerification, diagnostics::Error>
verify_release_asset_lock(const ReleaseAssetLock &lock,
                          const std::filesystem::path &repo_root);

// True when changing one entry hash changes the lock content digest.
[[nodiscard]] bool
release_lock_detects_modified_entry(const ReleaseAssetLock &baseline,
                                    std::string_view entry_id,
                                    std::string_view new_sha256);

} // namespace instantee::scene_ir
