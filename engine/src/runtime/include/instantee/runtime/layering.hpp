#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace instantee::runtime {

// One drawable contribution. Groups/cameras are skipped by the builder.
struct DrawItem {
  std::uint32_t node_index = 0;
  BlendMode blend = BlendMode::opaque;
  double opacity = 1.0;
  std::int32_t layer_order = 0;
  // World-space depth used for ordering (matrix translation z). Smaller z is
  // farther when content sits in front of a -Z-facing camera.
  double depth_z = 0.0;
  std::string_view node_id;
};

// Opaque batch order is stable but may later be regrouped by material without
// changing transparent_paint. Transparent paint is back-to-front:
// layer_order ↑, then depth_z ↑ (farther first), then node_id ↑.
struct DrawOrder {
  std::vector<DrawItem> opaque_batch;
  std::vector<DrawItem> transparent_paint;
};

[[nodiscard]] constexpr bool is_transparent_draw(BlendMode blend,
                                                 double opacity) noexcept {
  if (blend != BlendMode::opaque) {
    return true;
  }
  return opacity < 1.0 - 1e-12;
}

// depth_z_per_node must align with scene.nodes(). Invisible nodes and
// non-drawable kinds (group, camera) are omitted.
[[nodiscard]] std::expected<DrawOrder, diagnostics::Error>
build_draw_order(const CompiledScene &scene,
                 std::span<const double> depth_z_per_node);

// Convenience: world-matrix translation z as depth.
[[nodiscard]] std::expected<DrawOrder, diagnostics::Error>
build_draw_order_from_world(const CompiledScene &scene);

} // namespace instantee::runtime
