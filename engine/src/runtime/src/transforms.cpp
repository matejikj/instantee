#include <instantee/runtime/transforms.hpp>

#include <limits>
#include <string_view>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

core::Mat4 world_at(std::uint32_t index, std::span<const RuntimeNode> nodes,
                    std::vector<core::Mat4> &world,
                    std::vector<std::uint8_t> &state) {
  // state: 0 = unseen, 1 = visiting, 2 = done
  if (state[index] == 2) {
    return world[index];
  }
  if (state[index] == 1) {
    // Should be unreachable after make_compiled_scene cycle rejection.
    world[index] = core::Mat4::identity();
    state[index] = 2;
    return world[index];
  }
  state[index] = 1;
  const RuntimeNode &node = nodes[index];
  const core::Mat4 local = local_matrix(node.transform);
  if (!node.parent_index.has_value()) {
    world[index] = local;
  } else {
    const core::Mat4 parent = world_at(*node.parent_index, nodes, world, state);
    world[index] = core::combine_parent_local(parent, local);
  }
  state[index] = 2;
  return world[index];
}

} // namespace

std::expected<std::vector<core::Mat4>, diagnostics::Error>
compute_world_matrices(std::span<const RuntimeNode> nodes) {
  if (nodes.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return std::unexpected(
        err("resource_budget_exceeded", "Too many runtime nodes"));
  }
  for (std::uint32_t index = 0; index < nodes.size(); ++index) {
    const auto &parent = nodes[index].parent_index;
    if (parent.has_value() && *parent >= nodes.size()) {
      return std::unexpected(
          err("dangling_reference", "parent_index out of range"));
    }
  }

  std::vector<core::Mat4> world(nodes.size(), core::Mat4::identity());
  std::vector<std::uint8_t> state(nodes.size(), 0);
  for (std::uint32_t index = 0; index < nodes.size(); ++index) {
    (void)world_at(index, nodes, world, state);
  }
  return world;
}

} // namespace instantee::runtime
