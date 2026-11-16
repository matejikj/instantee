#include <instantee/core_model/ids.hpp>

#include <string>

namespace {

using instantee::core::AssetId;
using instantee::core::FilmId;
using instantee::core::IdRegistry;
using instantee::core::MaterialId;
using instantee::core::NodeId;
using instantee::core::ShotId;
using instantee::diagnostics::ErrorKind;
using instantee::diagnostics::ExitCode;

[[nodiscard]] int check_stable_values() {
  const auto first = FilmId::make("editorial-demo");
  const auto second = FilmId::make("editorial-demo");
  if (!first || !second || first->value() != second->value() ||
      first->value() != "editorial-demo") {
    return 1;
  }
  return 0;
}

[[nodiscard]] int check_invalid_ids() {
  if (FilmId::make("") || NodeId::make("a/b") || AssetId::make("x\\y")) {
    return 2;
  }
  return 0;
}

[[nodiscard]] int check_collision_diagnostics() {
  IdRegistry registry;
  const auto shot = ShotId::make("intro");
  const auto node = NodeId::make("title");
  const auto material = MaterialId::make("matte");
  const auto asset = AssetId::make("logo");
  if (!shot || !node || !material || !asset) {
    return 3;
  }
  if (!registry.register_shot(*shot, "/shots/0/id") ||
      !registry.register_node(*node, "/shots/0/nodes/0/id") ||
      !registry.register_material(*material) ||
      !registry.register_asset(*asset)) {
    return 4;
  }

  const auto collision = registry.register_shot(*shot, "/shots/1/id");
  if (collision || collision.error().code != "id_collision" ||
      collision.error().kind != ErrorKind::invalid_input ||
      collision.error().exit_code != ExitCode::invalid_input ||
      !collision.error().context.json_path ||
      *collision.error().context.json_path != "/shots/1/id") {
    return 5;
  }

  // Same spelling in another namespace remains valid.
  const auto as_node = NodeId::make("intro");
  if (!as_node || !registry.register_node(*as_node, "/nodes/intro")) {
    return 6;
  }
  if (!registry.contains_shot(*shot) || !registry.contains_node(*node) ||
      !registry.contains_material(*material) ||
      !registry.contains_asset(*asset)) {
    return 7;
  }
  return 0;
}

} // namespace

int main() {
  if (const int code = check_stable_values(); code != 0) {
    return code;
  }
  if (const int code = check_invalid_ids(); code != 0) {
    return code;
  }
  return check_collision_diagnostics();
}
