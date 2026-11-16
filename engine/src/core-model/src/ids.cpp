#include <instantee/core_model/ids.hpp>

#include <utility>

namespace instantee::core {

std::expected<void, diagnostics::Error>
IdRegistry::insert_unique(std::unordered_set<std::string> &table,
                          std::string_view value, std::string_view kind,
                          std::string_view json_path) {
  const auto [_, inserted] = table.emplace(value);
  if (!inserted) {
    diagnostics::Error error{
        diagnostics::ExitCode::invalid_input, "id_collision", "ids",
        std::string("Duplicate ") + std::string(kind) + " identifier"};
    if (!json_path.empty()) {
      error = std::move(error).with_json_path(std::string(json_path));
    }
    return std::unexpected(std::move(error));
  }
  return {};
}

std::expected<void, diagnostics::Error>
IdRegistry::register_film(const FilmId &id, std::string_view json_path) {
  return insert_unique(films_, id.value(), "film", json_path);
}

std::expected<void, diagnostics::Error>
IdRegistry::register_shot(const ShotId &id, std::string_view json_path) {
  return insert_unique(shots_, id.value(), "shot", json_path);
}

std::expected<void, diagnostics::Error>
IdRegistry::register_node(const NodeId &id, std::string_view json_path) {
  return insert_unique(nodes_, id.value(), "node", json_path);
}

std::expected<void, diagnostics::Error>
IdRegistry::register_material(const MaterialId &id,
                              std::string_view json_path) {
  return insert_unique(materials_, id.value(), "material", json_path);
}

std::expected<void, diagnostics::Error>
IdRegistry::register_asset(const AssetId &id, std::string_view json_path) {
  return insert_unique(assets_, id.value(), "asset", json_path);
}

bool IdRegistry::contains_film(const FilmId &id) const {
  return films_.contains(std::string(id.value()));
}

bool IdRegistry::contains_shot(const ShotId &id) const {
  return shots_.contains(std::string(id.value()));
}

bool IdRegistry::contains_node(const NodeId &id) const {
  return nodes_.contains(std::string(id.value()));
}

bool IdRegistry::contains_material(const MaterialId &id) const {
  return materials_.contains(std::string(id.value()));
}

bool IdRegistry::contains_asset(const AssetId &id) const {
  return assets_.contains(std::string(id.value()));
}

} // namespace instantee::core
