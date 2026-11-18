#include <instantee/render_core/paper_prop_blueprint.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_prop_blueprint", message};
}

[[nodiscard]] bool finite_positive(float v) noexcept {
  return std::isfinite(v) && v > 0.0f;
}

[[nodiscard]] bool finite_nonneg(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f;
}

[[nodiscard]] bool part_inside_frame(const PaperPropPart &part,
                                     float frame_width,
                                     float frame_height) noexcept {
  if (!is_valid_rect(part.local_bounds) ||
      !std::isfinite(part.local_bounds.x) ||
      !std::isfinite(part.local_bounds.y)) {
    return false;
  }
  const float x1 = part.local_bounds.x + part.local_bounds.width;
  const float y1 = part.local_bounds.y + part.local_bounds.height;
  return part.local_bounds.x >= 0.0f && part.local_bounds.y >= 0.0f &&
         x1 <= frame_width + 1e-4f && y1 <= frame_height + 1e-4f;
}

[[nodiscard]] Rect2f scale_rect(const Rect2f &rect, float scale) noexcept {
  return Rect2f{rect.x * scale, rect.y * scale, rect.width * scale,
                rect.height * scale};
}

} // namespace

std::expected<PaperPropKind, diagnostics::Error>
parse_paper_prop_kind(std::string_view text) {
  if (text == "instrument") {
    return PaperPropKind::instrument;
  }
  if (text == "building") {
    return PaperPropKind::building;
  }
  if (text == "cloud") {
    return PaperPropKind::cloud;
  }
  return std::unexpected(
      make_invalid("unknown_prop_kind", "Unknown paper prop kind"));
}

PaperPropPalette make_default_paper_prop_palette() noexcept {
  PaperPropPalette palette;
  palette.slots = {
      {"body", Rgba8{232, 220, 198, 255}}, {"accent", Rgba8{176, 74, 58, 255}},
      {"ink", Rgba8{42, 36, 32, 255}},     {"sky", Rgba8{214, 228, 236, 255}},
      {"shadow", Rgba8{0, 0, 0, 64}},
  };
  return palette;
}

PaperPropMaterialSet make_default_paper_prop_materials() noexcept {
  PaperPropMaterialSet materials;
  materials.slots["stock"] = paper_material_defaults_v1();
  PaperMaterialParams ink = paper_material_defaults_v1();
  ink.fiber = 0.2f;
  ink.roughness = 0.55f;
  ink.color_wash = 0.05f;
  ink.seed = 3;
  materials.slots["ink"] = ink;
  PaperMaterialParams soft = paper_material_defaults_v1();
  soft.fiber = 0.55f;
  soft.roughness = 0.25f;
  soft.edge_thickness = 0.02f;
  soft.seed = 7;
  materials.slots["soft"] = soft;
  return materials;
}

PaperPropBlueprint make_instrument_prop_blueprint(float frame_width,
                                                  float frame_height,
                                                  float scale) {
  PaperPropBlueprint bp;
  bp.id = "prop.instrument.v1";
  bp.kind = PaperPropKind::instrument;
  bp.frame_width = frame_width;
  bp.frame_height = frame_height;
  bp.scale = scale;

  const float w = frame_width;
  const float h = frame_height;
  bp.parts.push_back(PaperPropPart{
      .id = "body",
      .shape = PaperPropPartShape::rounded_rect,
      .local_bounds = {w * 0.08f, h * 0.28f, w * 0.84f, h * 0.44f},
      .corner_radius = h * 0.08f,
      .palette_slot = "body",
      .material_slot = "stock",
      .layer_order = 0,
      .depth_offset = 0.0f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "dial",
      .shape = PaperPropPartShape::ellipse,
      .local_bounds = {w * 0.38f, h * 0.34f, w * 0.24f, h * 0.32f},
      .corner_radius = 0,
      .palette_slot = "accent",
      .material_slot = "ink",
      .layer_order = 1,
      .depth_offset = 0.4f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "needle",
      .shape = PaperPropPartShape::rect,
      .local_bounds = {w * 0.48f, h * 0.18f, w * 0.04f, h * 0.28f},
      .corner_radius = 0,
      .palette_slot = "ink",
      .material_slot = "ink",
      .layer_order = 2,
      .depth_offset = 0.8f,
  });
  return bp;
}

PaperPropBlueprint make_building_prop_blueprint(float frame_width,
                                                float frame_height,
                                                float scale) {
  PaperPropBlueprint bp;
  bp.id = "prop.building.v1";
  bp.kind = PaperPropKind::building;
  bp.frame_width = frame_width;
  bp.frame_height = frame_height;
  bp.scale = scale;

  const float w = frame_width;
  const float h = frame_height;
  bp.parts.push_back(PaperPropPart{
      .id = "facade",
      .shape = PaperPropPartShape::rect,
      .local_bounds = {w * 0.15f, h * 0.28f, w * 0.70f, h * 0.62f},
      .palette_slot = "body",
      .material_slot = "stock",
      .layer_order = 0,
      .depth_offset = 0.0f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "roof",
      .shape = PaperPropPartShape::rect,
      .local_bounds = {w * 0.08f, h * 0.16f, w * 0.84f, h * 0.16f},
      .palette_slot = "accent",
      .material_slot = "stock",
      .layer_order = 1,
      .depth_offset = 0.3f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "door",
      .shape = PaperPropPartShape::rounded_rect,
      .local_bounds = {w * 0.42f, h * 0.62f, w * 0.16f, h * 0.28f},
      .corner_radius = w * 0.02f,
      .palette_slot = "ink",
      .material_slot = "ink",
      .layer_order = 2,
      .depth_offset = 0.5f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "window_l",
      .shape = PaperPropPartShape::rect,
      .local_bounds = {w * 0.24f, h * 0.40f, w * 0.14f, h * 0.12f},
      .palette_slot = "sky",
      .material_slot = "soft",
      .layer_order = 3,
      .depth_offset = 0.55f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "window_r",
      .shape = PaperPropPartShape::rect,
      .local_bounds = {w * 0.62f, h * 0.40f, w * 0.14f, h * 0.12f},
      .palette_slot = "sky",
      .material_slot = "soft",
      .layer_order = 3,
      .depth_offset = 0.55f,
  });
  return bp;
}

PaperPropBlueprint make_cloud_prop_blueprint(float frame_width,
                                             float frame_height, float scale) {
  PaperPropBlueprint bp;
  bp.id = "prop.cloud.v1";
  bp.kind = PaperPropKind::cloud;
  bp.frame_width = frame_width;
  bp.frame_height = frame_height;
  bp.scale = scale;

  const float w = frame_width;
  const float h = frame_height;
  bp.parts.push_back(PaperPropPart{
      .id = "puff_a",
      .shape = PaperPropPartShape::ellipse,
      .local_bounds = {w * 0.08f, h * 0.30f, w * 0.42f, h * 0.48f},
      .palette_slot = "sky",
      .material_slot = "soft",
      .layer_order = 0,
      .depth_offset = 0.0f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "puff_b",
      .shape = PaperPropPartShape::ellipse,
      .local_bounds = {w * 0.30f, h * 0.12f, w * 0.46f, h * 0.52f},
      .palette_slot = "body",
      .material_slot = "soft",
      .layer_order = 1,
      .depth_offset = 0.25f,
  });
  bp.parts.push_back(PaperPropPart{
      .id = "puff_c",
      .shape = PaperPropPartShape::ellipse,
      .local_bounds = {w * 0.55f, h * 0.28f, w * 0.38f, h * 0.46f},
      .palette_slot = "sky",
      .material_slot = "soft",
      .layer_order = 2,
      .depth_offset = 0.15f,
  });
  return bp;
}

std::expected<void, diagnostics::Error>
validate_paper_prop_blueprint(const PaperPropBlueprint &blueprint) {
  if (blueprint.id.empty()) {
    return std::unexpected(
        make_invalid("empty_blueprint_id", "Blueprint id must be non-empty"));
  }
  if (!finite_positive(blueprint.frame_width) ||
      !finite_positive(blueprint.frame_height) ||
      !finite_positive(blueprint.scale)) {
    return std::unexpected(
        make_invalid("invalid_blueprint_frame",
                     "Blueprint frame size and scale must be finite and > 0"));
  }
  if (blueprint.parts.empty()) {
    return std::unexpected(make_invalid("empty_blueprint_parts",
                                        "Blueprint needs at least one part"));
  }
  if (blueprint.parts.size() > 64) {
    return std::unexpected(make_invalid(
        "too_many_parts", "Blueprint part count exceeds the v1 budget of 64"));
  }

  for (std::size_t i = 0; i < blueprint.parts.size(); ++i) {
    const PaperPropPart &part = blueprint.parts[i];
    if (part.id.empty() || part.palette_slot.empty() ||
        part.material_slot.empty()) {
      return std::unexpected(make_invalid(
          "invalid_part_identity",
          "Each part needs non-empty id, palette_slot and material_slot"));
    }
    if (!part_inside_frame(part, blueprint.frame_width,
                           blueprint.frame_height)) {
      return std::unexpected(make_invalid(
          "part_outside_frame",
          "Part bounds must be finite, positive and inside the prop frame"));
    }
    if (!finite_nonneg(part.corner_radius) ||
        !finite_nonneg(part.depth_offset)) {
      return std::unexpected(make_invalid(
          "invalid_part_metrics",
          "Part corner_radius and depth_offset must be finite and >= 0"));
    }
    if (part.shape == PaperPropPartShape::rounded_rect) {
      const float max_r =
          0.5f * std::min(part.local_bounds.width, part.local_bounds.height);
      if (part.corner_radius > max_r + 1e-4f) {
        return std::unexpected(make_invalid(
            "invalid_corner_radius",
            "Rounded-rect corner radius exceeds half the shorter side"));
      }
    }
    for (std::size_t j = i + 1; j < blueprint.parts.size(); ++j) {
      if (blueprint.parts[j].id == part.id) {
        return std::unexpected(make_invalid(
            "duplicate_part_id", "Blueprint part ids must be unique"));
      }
    }
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_paper_prop_bindings(const PaperPropBlueprint &blueprint,
                             const PaperPropPalette &palette,
                             const PaperPropMaterialSet &materials) {
  if (auto ok = validate_paper_prop_blueprint(blueprint); !ok) {
    return ok;
  }
  for (const PaperPropPart &part : blueprint.parts) {
    const auto color = palette.slots.find(part.palette_slot);
    if (color == palette.slots.end()) {
      return std::unexpected(make_invalid(
          "missing_palette_slot",
          "Blueprint references a palette slot that is not bound"));
    }
    const auto mat = materials.slots.find(part.material_slot);
    if (mat == materials.slots.end()) {
      return std::unexpected(make_invalid(
          "missing_material_slot",
          "Blueprint references a material slot that is not bound"));
    }
    if (auto ok = validate_paper_material_params(mat->second); !ok) {
      return std::unexpected(ok.error());
    }
  }
  return {};
}

std::expected<ResolvedPaperProp, diagnostics::Error>
instantiate_paper_prop(const PaperPropBlueprint &blueprint,
                       const PaperPropPalette &palette,
                       const PaperPropMaterialSet &materials) {
  if (auto ok = validate_paper_prop_bindings(blueprint, palette, materials);
      !ok) {
    return std::unexpected(ok.error());
  }

  ResolvedPaperProp out;
  out.blueprint_id = blueprint.id;
  out.kind = blueprint.kind;
  out.frame_width = blueprint.frame_width * blueprint.scale;
  out.frame_height = blueprint.frame_height * blueprint.scale;
  out.scale = blueprint.scale;
  out.parts.reserve(blueprint.parts.size());

  for (const PaperPropPart &part : blueprint.parts) {
    ResolvedPaperPropPart resolved;
    resolved.id = part.id;
    resolved.shape = part.shape;
    resolved.bounds = scale_rect(part.local_bounds, blueprint.scale);
    resolved.corner_radius = part.corner_radius * blueprint.scale;
    resolved.palette_slot = part.palette_slot;
    resolved.material_slot = part.material_slot;
    resolved.color = palette.slots.at(part.palette_slot);
    resolved.material = materials.slots.at(part.material_slot);
    resolved.layer_order = part.layer_order;
    resolved.depth_offset = part.depth_offset;
    out.parts.push_back(std::move(resolved));
  }

  std::stable_sort(
      out.parts.begin(), out.parts.end(),
      [](const ResolvedPaperPropPart &a, const ResolvedPaperPropPart &b) {
        if (a.layer_order != b.layer_order) {
          return a.layer_order < b.layer_order;
        }
        if (a.depth_offset != b.depth_offset) {
          return a.depth_offset < b.depth_offset;
        }
        return a.id < b.id;
      });
  return out;
}

std::string
format_paper_prop_blueprint_snapshot(std::string_view label,
                                     const PaperPropBlueprint &blueprint,
                                     const ResolvedPaperProp &resolved) {
  std::ostringstream ss;
  ss << "name=" << label << " model=" << kPaperPropBlueprintModelVersion
     << " blueprint=" << blueprint.id
     << " kind=" << paper_prop_kind_id(blueprint.kind)
     << " frame=" << blueprint.frame_width << "x" << blueprint.frame_height
     << " scale=" << blueprint.scale << " parts=" << blueprint.parts.size()
     << '\n';
  for (const PaperPropPart &part : blueprint.parts) {
    ss << "part id=" << part.id
       << " shape=" << paper_prop_part_shape_id(part.shape)
       << " bounds=" << part.local_bounds.x << "," << part.local_bounds.y << ","
       << part.local_bounds.width << "," << part.local_bounds.height
       << " corner=" << part.corner_radius << " palette=" << part.palette_slot
       << " material=" << part.material_slot << " layer=" << part.layer_order
       << " depth=" << part.depth_offset << '\n';
  }
  ss << "resolved frame=" << resolved.frame_width << "x"
     << resolved.frame_height << '\n';
  for (const ResolvedPaperPropPart &part : resolved.parts) {
    ss << "resolved id=" << part.id
       << " shape=" << paper_prop_part_shape_id(part.shape)
       << " bounds=" << part.bounds.x << "," << part.bounds.y << ","
       << part.bounds.width << "," << part.bounds.height
       << " rgba=" << static_cast<int>(part.color.r) << ","
       << static_cast<int>(part.color.g) << ","
       << static_cast<int>(part.color.b) << ","
       << static_cast<int>(part.color.a)
       << " material_seed=" << part.material.seed
       << " fiber=" << part.material.fiber
       << " roughness=" << part.material.roughness << '\n';
  }
  return ss.str();
}

} // namespace instantee::render_core
