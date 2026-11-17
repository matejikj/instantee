#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <expected>
#include <vector>

namespace instantee::runtime {

[[nodiscard]] inline core::LocalTransform
to_local_transform(const RuntimeTransform &transform) noexcept {
  return core::LocalTransform{
      .translate = {transform.translate_x, transform.translate_y,
                    transform.translate_z},
      .scale = {transform.scale_x, transform.scale_y, transform.scale_z},
      .rotation_z_rad = transform.rotation_z_rad,
      .pivot = {transform.pivot_x, transform.pivot_y, transform.pivot_z},
  };
}

[[nodiscard]] inline core::Mat4
local_matrix(const RuntimeTransform &transform) noexcept {
  return core::local_matrix(to_local_transform(transform));
}

// World matrices for every node in document order. world[i] =
// world[parent] * local[i] (identity parent for roots). Requires an acyclic
// parent graph (already enforced by make_compiled_scene).
[[nodiscard]] std::expected<std::vector<core::Mat4>, diagnostics::Error>
compute_world_matrices(std::span<const RuntimeNode> nodes);

[[nodiscard]] inline std::expected<std::vector<core::Mat4>, diagnostics::Error>
compute_world_matrices(const CompiledScene &scene) {
  return compute_world_matrices(scene.nodes());
}

} // namespace instantee::runtime
