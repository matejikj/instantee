#include <instantee/scene_ir/migrate.hpp>

#include <yyjson.h>

#include <cctype>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error
err(std::string_view code, std::string_view message, std::string path,
    diagnostics::ExitCode exit_code = diagnostics::ExitCode::invalid_input) {
  const auto kind = diagnostics::Error::kind_for(exit_code);
  return diagnostics::Error{exit_code, kind, code, "migrate", message}
      .with_json_path(std::move(path));
}

struct DocGuard {
  yyjson_doc *doc = nullptr;
  ~DocGuard() { yyjson_doc_free(doc); }
  DocGuard(const DocGuard &) = delete;
  DocGuard &operator=(const DocGuard &) = delete;
  explicit DocGuard(yyjson_doc *owned) : doc(owned) {}
};

struct MutGuard {
  yyjson_mut_doc *doc = nullptr;
  ~MutGuard() { yyjson_mut_doc_free(doc); }
  MutGuard(const MutGuard &) = delete;
  MutGuard &operator=(const MutGuard &) = delete;
  explicit MutGuard(yyjson_mut_doc *owned) : doc(owned) {}
};

std::expected<std::uint32_t, diagnostics::Error>
parse_component(std::string_view text, std::string_view path) {
  if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.front()))) {
    return std::unexpected(err("invalid_semver",
                               "Invalid schema_version component",
                               std::string(path)));
  }
  if (text.size() > 1 && text.front() == '0') {
    return std::unexpected(
        err("invalid_semver", "Leading zeros are not allowed in schema_version",
            std::string(path)));
  }
  std::uint64_t value = 0;
  for (unsigned char ch : text) {
    if (!std::isdigit(ch)) {
      return std::unexpected(err("invalid_semver",
                                 "Invalid schema_version component",
                                 std::string(path)));
    }
    value = value * 10ULL + static_cast<std::uint64_t>(ch - '0');
    if (value > 0xFFFFFFFFULL) {
      return std::unexpected(err("invalid_semver",
                                 "schema_version component overflow",
                                 std::string(path)));
    }
  }
  return static_cast<std::uint32_t>(value);
}

std::expected<std::string, diagnostics::Error>
migrate_legacy_090(yyjson_val *root) {
  MutGuard mut{yyjson_mut_doc_new(nullptr)};
  if (mut.doc == nullptr) {
    return std::unexpected(err("out_of_memory",
                               "Cannot allocate migration document", "/",
                               diagnostics::ExitCode::out_of_memory));
  }
  yyjson_mut_val *obj = yyjson_val_mut_copy(mut.doc, root);
  if (obj == nullptr || !yyjson_mut_is_obj(obj)) {
    return std::unexpected(err("type_mismatch", "Root must be an object", "/"));
  }
  yyjson_mut_doc_set_root(mut.doc, obj);

  yyjson_mut_val *style = yyjson_mut_obj_get(obj, "style");
  yyjson_mut_val *style_package = yyjson_mut_obj_get(obj, "style_package");
  if (style != nullptr && style_package != nullptr) {
    return std::unexpected(
        err("ambiguous_style_field",
            "Legacy document must not define both style and style_package",
            "/style_package", diagnostics::ExitCode::invalid_input));
  }
  if (style_package == nullptr || !yyjson_mut_is_str(style_package)) {
    return std::unexpected(err("missing_field",
                               "Legacy 0.9.0 requires string style_package",
                               "/style_package"));
  }
  if (style != nullptr) {
    return std::unexpected(err("unexpected_field",
                               "Legacy 0.9.0 must not define style yet",
                               "/style"));
  }

  const char *package = yyjson_mut_get_str(style_package);
  const size_t package_len = yyjson_mut_get_len(style_package);
  if (!yyjson_mut_obj_remove_key(obj, "style_package")) {
    return std::unexpected(
        err("migrate_failed", "Cannot remove style_package", "/style_package"));
  }
  if (!yyjson_mut_obj_add_strncpy(mut.doc, obj, "style", package,
                                  package_len)) {
    return std::unexpected(
        err("migrate_failed", "Cannot write style", "/style"));
  }
  if (!yyjson_mut_obj_remove_key(obj, "schema_version")) {
    return std::unexpected(err("migrate_failed", "Cannot update schema_version",
                               "/schema_version"));
  }
  if (!yyjson_mut_obj_add_strcpy(
          mut.doc, obj, "schema_version",
          std::string(kAuthoringSchemaCurrent).c_str())) {
    return std::unexpected(err("migrate_failed", "Cannot write schema_version",
                               "/schema_version"));
  }

  size_t length = 0;
  char *written = yyjson_mut_write(mut.doc, 0, &length);
  if (written == nullptr) {
    return std::unexpected(
        err("migrate_failed", "Cannot serialize migrated document", "/"));
  }
  std::string out(written, length);
  free(written);
  return out;
}

} // namespace

std::expected<SemVer, diagnostics::Error>
parse_schema_semver(std::string_view text) {
  const auto first = text.find('.');
  const auto second = first == std::string_view::npos
                          ? std::string_view::npos
                          : text.find('.', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      text.find('.', second + 1) != std::string_view::npos) {
    return std::unexpected(err("invalid_semver",
                               "schema_version must be MAJOR.MINOR.PATCH",
                               "/schema_version"));
  }
  auto major = parse_component(text.substr(0, first), "/schema_version");
  if (!major) {
    return std::unexpected(major.error());
  }
  auto minor = parse_component(text.substr(first + 1, second - first - 1),
                               "/schema_version");
  if (!minor) {
    return std::unexpected(minor.error());
  }
  auto patch = parse_component(text.substr(second + 1), "/schema_version");
  if (!patch) {
    return std::unexpected(patch.error());
  }
  return SemVer{*major, *minor, *patch};
}

std::expected<std::string, diagnostics::Error>
migrate_authoring_json(std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    return std::unexpected(err("invalid_json", "JSON input is empty", "/"));
  }
  std::string buffer(reinterpret_cast<const char *>(bytes.data()),
                     bytes.size());
  yyjson_read_err read_err{};
  DocGuard guard{
      yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &read_err)};
  if (guard.doc == nullptr) {
    return std::unexpected(
        err("invalid_json",
            read_err.msg != nullptr ? read_err.msg : "JSON parse failed", "/"));
  }
  yyjson_val *root = yyjson_doc_get_root(guard.doc);
  if (root == nullptr || !yyjson_is_obj(root)) {
    return std::unexpected(err("type_mismatch", "Root must be an object", "/"));
  }
  yyjson_val *version_val = yyjson_obj_get(root, "schema_version");
  if (version_val == nullptr || !yyjson_is_str(version_val)) {
    return std::unexpected(
        err("missing_field", "schema_version is required", "/schema_version"));
  }
  const std::string_view version(yyjson_get_str(version_val),
                                 yyjson_get_len(version_val));
  auto semver = parse_schema_semver(version);
  if (!semver) {
    return std::unexpected(semver.error());
  }
  if (semver->major != 0 && semver->major != 1) {
    return std::unexpected(
        err("unsupported_schema", "Unsupported authoring schema major version",
            "/schema_version", diagnostics::ExitCode::incompatible_schema));
  }
  if (version == kAuthoringSchemaCurrent) {
    return std::string(reinterpret_cast<const char *>(bytes.data()),
                       bytes.size());
  }
  if (version == kAuthoringSchemaLegacy090) {
    return migrate_legacy_090(root);
  }
  return std::unexpected(
      err("unsupported_schema", "Unsupported authoring schema_version",
          "/schema_version", diagnostics::ExitCode::incompatible_schema));
}

} // namespace instantee::scene_ir
