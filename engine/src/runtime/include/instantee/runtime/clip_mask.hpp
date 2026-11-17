#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/runtime/compiled_scene.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace instantee::runtime {

// Axis-aligned clip region in the defining node's local XY (canvas pixels).
// Distinct from pivot, anchor, and geometry bounds (SAS §8.1). No GPU types.
struct ClipRect {
  double min_x = 0;
  double min_y = 0;
  double max_x = 0;
  double max_y = 0;
};

[[nodiscard]] constexpr bool
is_finite_clip_rect(const ClipRect &rect) noexcept {
  return std::isfinite(rect.min_x) && std::isfinite(rect.min_y) &&
         std::isfinite(rect.max_x) && std::isfinite(rect.max_y);
}

[[nodiscard]] constexpr bool
is_normalized_clip_rect(const ClipRect &rect) noexcept {
  return rect.min_x <= rect.max_x && rect.min_y <= rect.max_y;
}

[[nodiscard]] constexpr bool is_empty_clip_rect(const ClipRect &rect) noexcept {
  return rect.min_x > rect.max_x || rect.min_y > rect.max_y;
}

// Overlap of two axis-aligned rects. nullopt when the intersection has no area.
[[nodiscard]] std::optional<ClipRect>
intersect_clip_rects(const ClipRect &a, const ClipRect &b) noexcept;

// Conservative world-XY AABB of a local clip after an affine world transform.
[[nodiscard]] ClipRect
transform_clip_rect_to_world(const ClipRect &local,
                             const core::Mat4 &world) noexcept;

// Clip/mask attached to one scene node. The local clip constrains that node
// and its descendants after world transform. mask_node_index names a node
// whose geometry may later act as a visibility mask (logical reference only).
struct NodeClipMaskBinding {
  std::uint32_t node_index = 0;
  std::optional<ClipRect> local_clip;
  std::optional<std::uint32_t> mask_node_index;
};

struct ClipMaskProgram {
  std::vector<NodeClipMaskBinding> bindings;
};

// Resolved per-node clip/mask contribution for one frame. Platform-neutral:
// render backends consume these fields without domain knowledge of Metal.
struct EffectiveClipMask {
  // Local visibility AND-ed with every ancestor's local visibility.
  bool inherited_visible = true;
  // nullopt when no ancestor (including self) defines a clip region.
  std::optional<ClipRect> world_clip;
  // true when active clips exist but their world intersection is empty.
  bool clip_empty = false;
  // Nearest mask binding walking from the node toward the root.
  std::optional<std::uint32_t> mask_node_index;
};

// Rejects dangling node/mask indices, non-finite or denormalized clip rects,
// empty bindings, and duplicate bindings for the same node_index.
[[nodiscard]] std::expected<void, diagnostics::Error>
validate_clip_mask_program(const CompiledScene &scene,
                           const ClipMaskProgram &program);

// Resolve effective clip/mask state for every node. world_matrices must align
// with scene.nodes(). Visibility uses each RuntimeNode::visible flag.
[[nodiscard]] std::expected<std::vector<EffectiveClipMask>, diagnostics::Error>
resolve_clip_masks(const CompiledScene &scene, const ClipMaskProgram &program,
                   std::span<const core::Mat4> world_matrices);

} // namespace instantee::runtime
