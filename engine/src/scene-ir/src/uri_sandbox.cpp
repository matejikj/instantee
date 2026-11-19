#include <instantee/scene_ir/uri_sandbox.hpp>

#include <cctype>
#include <system_error>

namespace instantee::scene_ir {
namespace {

diagnostics::Error bad_uri(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input,
                            "unsafe_asset_uri", "scene_ir", message};
}

// True when `relative` is `assets` or `assets/...` with no `..` components.
[[nodiscard]] bool
is_under_assets_relative(const std::filesystem::path &relative) noexcept {
  auto it = relative.begin();
  if (it == relative.end() || *it != "assets") {
    return false;
  }
  ++it;
  for (; it != relative.end(); ++it) {
    if (*it == "..") {
      return false;
    }
  }
  return true;
}

} // namespace

bool is_safe_asset_uri_syntax(std::string_view uri) noexcept {
  if (uri.empty() || uri.size() > 512U) {
    return false;
  }
  // Reject Windows separators, URI schemes (':'), and empty path segments.
  if (uri.find('\\') != std::string_view::npos ||
      uri.find(':') != std::string_view::npos ||
      uri.find("//") != std::string_view::npos) {
    return false;
  }
  if (!uri.starts_with("assets/")) {
    return false;
  }
  // Lexical `..` anywhere (including `assets/foo/../x`) is forbidden before
  // canonicalize so syntax policy does not depend on filesystem state.
  if (uri.contains("..")) {
    return false;
  }
  for (const char ch : uri) {
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
resolve_asset_uri(const std::filesystem::path &project_root,
                  std::string_view uri) {
  if (project_root.empty()) {
    return std::unexpected(bad_uri("Project root is required for URI sandbox"));
  }
  if (!is_safe_asset_uri_syntax(uri)) {
    return std::unexpected(bad_uri("Asset URI failed syntax sandbox checks"));
  }

  std::error_code ec;
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(project_root, ec);
  if (ec) {
    return std::unexpected(bad_uri("Cannot canonicalize project root"));
  }

  // Follows symlinks. A link under assets/ that resolves outside project_root
  // or outside assets/ is rejected via lexically_relative below.
  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(root / std::string(uri), ec);
  if (ec) {
    return std::unexpected(bad_uri("Cannot canonicalize asset URI"));
  }

  const std::filesystem::path relative = candidate.lexically_relative(root);
  if (relative.empty() || relative.native().starts_with("..") ||
      (!relative.empty() && *relative.begin() == "..")) {
    return std::unexpected(bad_uri("Asset URI escapes the project root"));
  }
  if (!is_under_assets_relative(relative)) {
    return std::unexpected(bad_uri("Asset URI is outside the assets sandbox"));
  }
  return candidate;
}

} // namespace instantee::scene_ir
