#include <instantee/runtime/layering.hpp>

#include <instantee/runtime/transforms.hpp>

#include <algorithm>
#include <utility>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

bool is_drawable_kind(RuntimeNodeKind kind) noexcept {
  switch (kind) {
  case RuntimeNodeKind::geometry:
  case RuntimeNodeKind::text:
  case RuntimeNodeKind::image:
  case RuntimeNodeKind::puppet:
    return true;
  case RuntimeNodeKind::group:
  case RuntimeNodeKind::camera:
    return false;
  }
  return false;
}

bool transparent_less(const DrawItem &a, const DrawItem &b) noexcept {
  if (a.layer_order != b.layer_order) {
    return a.layer_order < b.layer_order;
  }
  if (a.depth_z != b.depth_z) {
    return a.depth_z < b.depth_z;
  }
  if (a.node_id != b.node_id) {
    return a.node_id < b.node_id;
  }
  return a.node_index < b.node_index;
}

bool opaque_less(const DrawItem &a, const DrawItem &b) noexcept {
  // Stable opaque order uses the same key so identical state always yields the
  // same sequence; batching may regroup later without touching transparent.
  return transparent_less(a, b);
}

} // namespace

std::expected<DrawOrder, diagnostics::Error>
build_draw_order(const CompiledScene &scene,
                 std::span<const double> depth_z_per_node) {
  const auto nodes = scene.nodes();
  const auto ids = scene.node_ids();
  if (nodes.size() != ids.size()) {
    return std::unexpected(
        err("length_mismatch", "node_ids and nodes must match"));
  }
  if (depth_z_per_node.size() != nodes.size()) {
    return std::unexpected(
        err("length_mismatch", "depth_z_per_node must match scene node count"));
  }

  DrawOrder order;
  order.opaque_batch.reserve(nodes.size());
  order.transparent_paint.reserve(nodes.size());

  for (std::uint32_t index = 0; index < nodes.size(); ++index) {
    const RuntimeNode &node = nodes[index];
    if (!node.visible || !is_drawable_kind(node.kind)) {
      continue;
    }
    DrawItem item;
    item.node_index = index;
    item.blend = node.blend;
    item.opacity = node.transform.opacity;
    item.layer_order = node.layer_order;
    item.depth_z = depth_z_per_node[index];
    item.node_id = ids[index];
    if (is_transparent_draw(item.blend, item.opacity)) {
      order.transparent_paint.push_back(item);
    } else {
      order.opaque_batch.push_back(item);
    }
  }

  std::stable_sort(order.opaque_batch.begin(), order.opaque_batch.end(),
                   opaque_less);
  std::stable_sort(order.transparent_paint.begin(),
                   order.transparent_paint.end(), transparent_less);
  return order;
}

std::expected<DrawOrder, diagnostics::Error>
build_draw_order_from_world(const CompiledScene &scene) {
  auto worlds = compute_world_matrices(scene);
  if (!worlds) {
    return std::unexpected(worlds.error());
  }
  std::vector<double> depth(worlds->size(), 0.0);
  for (std::size_t index = 0; index < worlds->size(); ++index) {
    depth[index] = (*worlds)[index](2, 3);
  }
  return build_draw_order(scene, depth);
}

} // namespace instantee::runtime
