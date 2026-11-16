#pragma once

#include <instantee/diagnostics/error.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace instantee::core {

// Opaque, owned identifier. Equality is exact string equality; allocation does
// not rewrite the author-supplied spelling, so repeated compilation of the same
// document keeps the same ID values.
template <typename Tag> class TypedId {
public:
  TypedId() = default;

  [[nodiscard]] static std::expected<TypedId, diagnostics::Error>
  make(std::string_view value) {
    if (value.empty()) {
      return std::unexpected(
          diagnostics::Error{diagnostics::ExitCode::invalid_input, "empty_id",
                             "ids", "Identifier must be non-empty"});
    }
    for (const char ch : value) {
      if (ch == '\0' || ch == '/' || ch == '\\') {
        return std::unexpected(diagnostics::Error{
            diagnostics::ExitCode::invalid_input, "invalid_id_charset", "ids",
            "Identifier contains a forbidden character"});
      }
    }
    TypedId id;
    id.value_.assign(value.begin(), value.end());
    return id;
  }

  [[nodiscard]] std::string_view value() const noexcept { return value_; }

  [[nodiscard]] friend bool operator==(const TypedId &,
                                       const TypedId &) noexcept = default;

private:
  std::string value_;
};

struct FilmIdTag;
struct ShotIdTag;
struct NodeIdTag;
struct MaterialIdTag;
struct AssetIdTag;

using FilmId = TypedId<FilmIdTag>;
using ShotId = TypedId<ShotIdTag>;
using NodeId = TypedId<NodeIdTag>;
using MaterialId = TypedId<MaterialIdTag>;
using AssetId = TypedId<AssetIdTag>;

// Per-namespace uniqueness. Registering the same value twice yields a typed
// collision diagnostic with owned JSON-path context; it does not mutate IDs.
class IdRegistry {
public:
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_film(const FilmId &id, std::string_view json_path = {});
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_shot(const ShotId &id, std::string_view json_path = {});
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_node(const NodeId &id, std::string_view json_path = {});
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_material(const MaterialId &id, std::string_view json_path = {});
  [[nodiscard]] std::expected<void, diagnostics::Error>
  register_asset(const AssetId &id, std::string_view json_path = {});

  [[nodiscard]] bool contains_film(const FilmId &id) const;
  [[nodiscard]] bool contains_shot(const ShotId &id) const;
  [[nodiscard]] bool contains_node(const NodeId &id) const;
  [[nodiscard]] bool contains_material(const MaterialId &id) const;
  [[nodiscard]] bool contains_asset(const AssetId &id) const;

private:
  [[nodiscard]] static std::expected<void, diagnostics::Error>
  insert_unique(std::unordered_set<std::string> &table, std::string_view value,
                std::string_view kind, std::string_view json_path);

  std::unordered_set<std::string> films_;
  std::unordered_set<std::string> shots_;
  std::unordered_set<std::string> nodes_;
  std::unordered_set<std::string> materials_;
  std::unordered_set<std::string> assets_;
};

} // namespace instantee::core
