#include <instantee/scene_ir/uri_sandbox.hpp>

#include <algorithm>
#include <cctype>
#include <system_error>

namespace instantee::scene_ir {
namespace {

diagnostics::Error bad_uri(std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input,
                            "unsafe_asset_uri", "scene_ir", message};
}

} // namespace

bool is_safe_asset_uri_syntax(std::string_view uri) noexcept {
  if (uri.empty() || uri.size() > 512U) {
    return false;
  }
  if (uri.find('\\') != std::string_view::npos ||
      uri.find(':') != std::string_view::npos ||
      uri.find("//") != std::string_view::npos) {
    return false;
  }
  if (!uri.starts_with("assets/")) {
    return false;
  }
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

  const std::filesystem::path candidate =
      std::filesystem::weakly_canonical(root / std::string(uri), ec);
  if (ec) {
    return std::unexpected(bad_uri("Cannot canonicalize asset URI"));
  }

  const std::string root_text = root.generic_string();
  const std::string candidate_text = candidate.generic_string();
  if (candidate_text != root_text &&
      !candidate_text.starts_with(root_text + "/")) {
    return std::unexpected(bad_uri("Asset URI escapes the project root"));
  }
  if (!candidate_text.starts_with(root_text + "/assets/") &&
      candidate_text != root_text + "/assets") {
    return std::unexpected(bad_uri("Asset URI is outside the assets sandbox"));
  }
  return candidate;
}

} // namespace instantee::scene_ir
