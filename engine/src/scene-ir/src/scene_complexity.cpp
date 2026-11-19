#include <instantee/scene_ir/scene_complexity.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace instantee::scene_ir {
namespace {

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "scene_complexity", message};
}

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "scene_complexity", message};
}

[[nodiscard]] std::uint64_t utf8_codepoints(std::string_view text) noexcept {
  std::uint64_t count = 0;
  for (unsigned char ch : text) {
    // Count leading bytes only (ASCII or UTF-8 lead).
    if ((ch & 0xC0U) != 0x80U) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] bool is_mask_feature(std::string_view feature) noexcept {
  // Conservative: any shader feature name advertising mask/clip contributes.
  return feature.find("mask") != std::string_view::npos ||
         feature.find("clip") != std::string_view::npos;
}

[[nodiscard]] bool is_draw_kind(NodeKind kind) noexcept {
  switch (kind) {
  case NodeKind::geometry:
  case NodeKind::text:
  case NodeKind::image:
  case NodeKind::puppet:
    return true;
  case NodeKind::group:
  case NodeKind::camera:
    return false;
  }
  return false;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
fail_if(bool exceeded, std::string_view code, std::string_view label,
        std::uint64_t actual, std::uint64_t limit) {
  if (!exceeded) {
    return {};
  }
  return std::unexpected(
      make_budget(code, std::format("{} {} exceeds complexity tier limit {}",
                                    label, actual, limit)));
}

} // namespace

SceneComplexityTier complexity_tier_p3_foundation_draft() noexcept {
  SceneComplexityTier tier;
  tier.id = "p3-foundation-draft";
  // Tightened structural ceilings for 1080×1920 draft reels. Large enough for
  // style fixtures; pathological growth fails before allocator blow-up.
  tier.limits.max_nodes = 2048;
  tier.limits.max_node_depth = 24;
  tier.limits.max_assets = 256;
  tier.limits.max_geometry = 1024;
  tier.limits.max_text_runs = 512;
  tier.limits.max_text_codepoints = 50'000;
  tier.limits.max_draw_items = 1536;
  tier.limits.max_mask_materials = 128;
  tier.limits.max_pixel_layers = 512;
  tier.limits.max_animation_tracks = 2048;
  tier.limits.max_keyframes = 32'768;
  tier.limits.max_shots = 128;
  tier.limits.max_canvas_width_px = 1080;
  tier.limits.max_canvas_height_px = 1920;
  return tier;
}

std::expected<SceneComplexityTier, diagnostics::Error>
lookup_complexity_tier(std::string_view tier_id) {
  if (tier_id.empty()) {
    return std::unexpected(
        make_invalid("complexity_tier_missing", "complexity tier id is empty"));
  }
  if (tier_id == "p3-foundation-draft") {
    return complexity_tier_p3_foundation_draft();
  }
  // Style reel ids inherit the same structural caps until a dedicated tighten.
  if (tier_id == "editorial-b01-v1" || tier_id == "notebook-b02-v1" ||
      tier_id == "paper-b03-v1") {
    auto tier = complexity_tier_p3_foundation_draft();
    tier.id = std::string(tier_id);
    return tier;
  }
  return std::unexpected(
      make_invalid("complexity_tier_unknown",
                   std::format("unknown complexity tier '{}'", tier_id)));
}

SceneComplexityMetrics
measure_scene_complexity(const SceneDocument &document) noexcept {
  SceneComplexityMetrics metrics{};
  metrics.nodes = static_cast<std::uint32_t>(document.nodes.size());
  metrics.assets = static_cast<std::uint32_t>(document.assets.size());
  metrics.geometry = static_cast<std::uint32_t>(document.geometry_ids.size());
  metrics.text_runs = static_cast<std::uint32_t>(document.text_runs.size());
  metrics.animation_tracks =
      static_cast<std::uint32_t>(document.animation_tracks.size());
  metrics.shots = static_cast<std::uint32_t>(document.shots.size());
  metrics.canvas_width_px = document.canvas_width;
  metrics.canvas_height_px = document.canvas_height;

  for (const auto &run : document.text_runs) {
    metrics.text_codepoints += utf8_codepoints(run.text);
  }
  for (const auto &track : document.animation_tracks) {
    metrics.keyframes += static_cast<std::uint64_t>(track.keyframes.size());
  }
  for (const auto &material : document.materials) {
    if (is_mask_feature(material.shader_feature)) {
      ++metrics.mask_materials;
    }
  }

  std::unordered_map<std::string, const Node *> by_id;
  by_id.reserve(document.nodes.size());
  for (const auto &node : document.nodes) {
    by_id.emplace(node.id, &node);
    if (is_draw_kind(node.kind)) {
      ++metrics.draw_items;
    }
  }

  // Parent-chain depth (edges). Cycles should already be rejected by
  // validate_graph; still bound the walk.
  for (const auto &node : document.nodes) {
    std::uint32_t depth = 0;
    std::unordered_set<std::string> seen;
    const Node *cursor = &node;
    while (cursor->parent_id) {
      if (!seen.insert(cursor->id).second) {
        break;
      }
      auto it = by_id.find(*cursor->parent_id);
      if (it == by_id.end()) {
        break;
      }
      ++depth;
      if (depth > 10'000U) {
        break;
      }
      cursor = it->second;
    }
    metrics.node_depth = std::max(metrics.node_depth, depth);
  }

  for (const auto &shot : document.shots) {
    std::uint32_t layers = 0;
    for (const auto &id : shot.node_ids) {
      auto it = by_id.find(id);
      if (it == by_id.end()) {
        continue;
      }
      if (it->second->kind != NodeKind::group &&
          it->second->kind != NodeKind::camera) {
        ++layers;
      }
    }
    metrics.pixel_layers = std::max(metrics.pixel_layers, layers);
  }

  return metrics;
}

std::expected<void, diagnostics::Error>
check_scene_complexity(const SceneDocument &document,
                       const SceneComplexityTier &tier) {
  if (tier.id.empty()) {
    return std::unexpected(
        make_invalid("complexity_tier_missing", "complexity tier id is empty"));
  }
  const auto m = measure_scene_complexity(document);
  const auto &L = tier.limits;

  if (auto r = fail_if(L.max_nodes != 0 && m.nodes > L.max_nodes,
                       "complexity.nodes", "node count", m.nodes, L.max_nodes);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_node_depth != 0 && m.node_depth > L.max_node_depth,
                       "complexity.node_depth", "node depth", m.node_depth,
                       L.max_node_depth);
      !r) {
    return r;
  }
  if (auto r =
          fail_if(L.max_assets != 0 && m.assets > L.max_assets,
                  "complexity.assets", "asset count", m.assets, L.max_assets);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_geometry != 0 && m.geometry > L.max_geometry,
                       "complexity.geometry", "geometry count", m.geometry,
                       L.max_geometry);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_text_runs != 0 && m.text_runs > L.max_text_runs,
                       "complexity.text_runs", "text run count", m.text_runs,
                       L.max_text_runs);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_text_codepoints != 0 &&
                           m.text_codepoints > L.max_text_codepoints,
                       "complexity.text_codepoints", "text codepoints",
                       m.text_codepoints, L.max_text_codepoints);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_draw_items != 0 && m.draw_items > L.max_draw_items,
                       "complexity.draw_items", "draw item count", m.draw_items,
                       L.max_draw_items);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_mask_materials != 0 &&
                           m.mask_materials > L.max_mask_materials,
                       "complexity.masks", "mask material count",
                       m.mask_materials, L.max_mask_materials);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_pixel_layers != 0 &&
                           m.pixel_layers > L.max_pixel_layers,
                       "complexity.pixel_layers", "pixel layer count",
                       m.pixel_layers, L.max_pixel_layers);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_animation_tracks != 0 &&
                           m.animation_tracks > L.max_animation_tracks,
                       "complexity.animation_tracks", "animation track count",
                       m.animation_tracks, L.max_animation_tracks);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_keyframes != 0 && m.keyframes > L.max_keyframes,
                       "complexity.keyframes", "keyframe count", m.keyframes,
                       L.max_keyframes);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_shots != 0 && m.shots > L.max_shots,
                       "complexity.shots", "shot count", m.shots, L.max_shots);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_canvas_width_px != 0 &&
                           m.canvas_width_px > L.max_canvas_width_px,
                       "complexity.canvas_width", "canvas width",
                       m.canvas_width_px, L.max_canvas_width_px);
      !r) {
    return r;
  }
  if (auto r = fail_if(L.max_canvas_height_px != 0 &&
                           m.canvas_height_px > L.max_canvas_height_px,
                       "complexity.canvas_height", "canvas height",
                       m.canvas_height_px, L.max_canvas_height_px);
      !r) {
    return r;
  }
  return {};
}

} // namespace instantee::scene_ir
