#include <instantee/runtime/clip_mask.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

} // namespace

std::optional<ClipRect> intersect_clip_rects(const ClipRect &a,
                                             const ClipRect &b) noexcept {
  ClipRect out;
  out.min_x = std::max(a.min_x, b.min_x);
  out.min_y = std::max(a.min_y, b.min_y);
  out.max_x = std::min(a.max_x, b.max_x);
  out.max_y = std::min(a.max_y, b.max_y);
  if (is_empty_clip_rect(out)) {
    return std::nullopt;
  }
  return out;
}

ClipRect transform_clip_rect_to_world(const ClipRect &local,
                                      const core::Mat4 &world) noexcept {
  const core::Vec3 corners[4] = {
      {local.min_x, local.min_y, 0.0},
      {local.max_x, local.min_y, 0.0},
      {local.min_x, local.max_y, 0.0},
      {local.max_x, local.max_y, 0.0},
  };
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const core::Vec3 &corner : corners) {
    const core::Vec3 world_pt = core::transform_point(world, corner);
    min_x = std::min(min_x, world_pt.x);
    min_y = std::min(min_y, world_pt.y);
    max_x = std::max(max_x, world_pt.x);
    max_y = std::max(max_y, world_pt.y);
  }
  return ClipRect{min_x, min_y, max_x, max_y};
}

std::expected<void, diagnostics::Error>
validate_clip_mask_program(const CompiledScene &scene,
                           const ClipMaskProgram &program) {
  const std::size_t node_count = scene.nodes().size();
  std::unordered_map<std::uint32_t, std::size_t> seen;
  seen.reserve(program.bindings.size());

  for (std::size_t index = 0; index < program.bindings.size(); ++index) {
    const NodeClipMaskBinding &binding = program.bindings[index];
    if (binding.node_index >= node_count) {
      return std::unexpected(
          err("dangling_reference", "clip binding node_index out of range"));
    }
    if (!binding.local_clip.has_value() &&
        !binding.mask_node_index.has_value()) {
      return std::unexpected(
          err("invalid_input",
              "clip/mask binding requires local_clip and/or mask_node_index"));
    }
    if (binding.local_clip.has_value()) {
      const ClipRect &rect = *binding.local_clip;
      if (!is_finite_clip_rect(rect)) {
        return std::unexpected(
            err("invalid_input", "clip rect components must be finite"));
      }
      if (!is_normalized_clip_rect(rect)) {
        return std::unexpected(
            err("invalid_input", "clip rect requires min <= max on both axes"));
      }
    }
    if (binding.mask_node_index.has_value()) {
      if (*binding.mask_node_index >= node_count) {
        return std::unexpected(
            err("dangling_reference", "mask_node_index out of range"));
      }
    }
    const auto [it, inserted] = seen.emplace(binding.node_index, index);
    if (!inserted) {
      return std::unexpected(
          err("invalid_input",
              "duplicate clip/mask binding for the same node_index"));
    }
  }
  return {};
}

std::expected<std::vector<EffectiveClipMask>, diagnostics::Error>
resolve_clip_masks(const CompiledScene &scene, const ClipMaskProgram &program,
                   std::span<const core::Mat4> world_matrices) {
  const auto &nodes = scene.nodes();
  if (world_matrices.size() != nodes.size()) {
    return std::unexpected(
        err("invalid_input", "world_matrices size must match node count"));
  }
  if (auto ok = validate_clip_mask_program(scene, program); !ok) {
    return std::unexpected(ok.error());
  }

  std::unordered_map<std::uint32_t, const NodeClipMaskBinding *> by_node;
  by_node.reserve(program.bindings.size());
  for (const NodeClipMaskBinding &binding : program.bindings) {
    by_node.emplace(binding.node_index, &binding);
  }

  std::vector<EffectiveClipMask> out(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    EffectiveClipMask &effective = out[index];

    bool visible = true;
    std::optional<std::uint32_t> mask;
    std::optional<ClipRect> world_clip;
    bool clip_empty = false;

    std::optional<std::uint32_t> cursor = static_cast<std::uint32_t>(index);
    while (cursor.has_value()) {
      const std::uint32_t node_index = *cursor;
      const RuntimeNode &node = nodes[node_index];
      visible = visible && node.visible;

      if (const auto it = by_node.find(node_index); it != by_node.end()) {
        const NodeClipMaskBinding &binding = *it->second;
        if (!mask.has_value() && binding.mask_node_index.has_value()) {
          mask = binding.mask_node_index;
        }
        if (!clip_empty && binding.local_clip.has_value()) {
          const ClipRect transformed = transform_clip_rect_to_world(
              *binding.local_clip, world_matrices[node_index]);
          if (!world_clip.has_value()) {
            world_clip = transformed;
          } else {
            auto hit = intersect_clip_rects(*world_clip, transformed);
            if (!hit) {
              clip_empty = true;
              world_clip.reset();
            } else {
              world_clip = *hit;
            }
          }
        }
      }

      cursor = node.parent_index;
    }

    effective.inherited_visible = visible;
    effective.world_clip = world_clip;
    effective.clip_empty = clip_empty;
    effective.mask_node_index = mask;
  }
  return out;
}

} // namespace instantee::runtime
