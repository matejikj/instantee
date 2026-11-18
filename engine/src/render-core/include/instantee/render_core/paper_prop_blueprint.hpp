#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/paper_material.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::render_core {

// Parametric paper props composed of flat parts. Blueprints own topology and
// slot names; palettes/materials bind at instantiate time so the same prop can
// be reused without editing part layout. No Metal types and no style-package
// name branches — slots are plain string ids.

inline constexpr std::string_view kPaperPropBlueprintModelVersion = "1.0.0";

enum class PaperPropKind : std::uint8_t {
  instrument = 1,
  building = 2,
  cloud = 3,
};

enum class PaperPropPartShape : std::uint8_t {
  rect = 1,
  ellipse = 2,
  rounded_rect = 3,
};

// One flat piece in local prop space (origin top-left of the prop frame).
struct PaperPropPart {
  std::string id;
  PaperPropPartShape shape = PaperPropPartShape::rect;
  Rect2f local_bounds{};
  float corner_radius = 0;
  // Palette / material slot names (must be non-empty; resolved at instantiate).
  std::string palette_slot;
  std::string material_slot;
  // Transparent draw order within the prop (then depth_offset, then id).
  std::uint32_t layer_order = 0;
  // Positive depthUnits offset relative to the prop root (2.5D cue).
  float depth_offset = 0;
};

struct PaperPropBlueprint {
  std::string id;
  PaperPropKind kind = PaperPropKind::instrument;
  // Authoring frame size in local pixels; parts must stay inside when scale=1.
  float frame_width = 1;
  float frame_height = 1;
  // Uniform scale applied when instantiating (> 0).
  float scale = 1;
  std::vector<PaperPropPart> parts{};
};

struct PaperPropPalette {
  std::unordered_map<std::string, Rgba8> slots{};
};

struct PaperPropMaterialSet {
  std::unordered_map<std::string, PaperMaterialParams> slots{};
};

struct ResolvedPaperPropPart {
  std::string id;
  PaperPropPartShape shape = PaperPropPartShape::rect;
  Rect2f bounds{};
  float corner_radius = 0;
  std::string palette_slot;
  std::string material_slot;
  Rgba8 color{};
  PaperMaterialParams material = paper_material_defaults_v1();
  std::uint32_t layer_order = 0;
  float depth_offset = 0;
};

struct ResolvedPaperProp {
  std::string blueprint_id;
  PaperPropKind kind = PaperPropKind::instrument;
  float frame_width = 1;
  float frame_height = 1;
  float scale = 1;
  std::vector<ResolvedPaperPropPart> parts{};
};

[[nodiscard]] constexpr std::string_view
paper_prop_kind_id(PaperPropKind kind) noexcept {
  switch (kind) {
  case PaperPropKind::instrument:
    return "instrument";
  case PaperPropKind::building:
    return "building";
  case PaperPropKind::cloud:
    return "cloud";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
paper_prop_part_shape_id(PaperPropPartShape shape) noexcept {
  switch (shape) {
  case PaperPropPartShape::rect:
    return "rect";
  case PaperPropPartShape::ellipse:
    return "ellipse";
  case PaperPropPartShape::rounded_rect:
    return "rounded_rect";
  }
  return "unknown";
}

[[nodiscard]] std::expected<PaperPropKind, diagnostics::Error>
parse_paper_prop_kind(std::string_view text);

// Curated parametric factories (deterministic part ids and layout).
[[nodiscard]] PaperPropBlueprint
make_instrument_prop_blueprint(float frame_width = 120, float frame_height = 80,
                               float scale = 1);
[[nodiscard]] PaperPropBlueprint
make_building_prop_blueprint(float frame_width = 100, float frame_height = 140,
                             float scale = 1);
[[nodiscard]] PaperPropBlueprint
make_cloud_prop_blueprint(float frame_width = 140, float frame_height = 70,
                          float scale = 1);

[[nodiscard]] PaperPropPalette make_default_paper_prop_palette() noexcept;
[[nodiscard]] PaperPropMaterialSet make_default_paper_prop_materials() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_prop_blueprint(const PaperPropBlueprint &blueprint);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_prop_bindings(const PaperPropBlueprint &blueprint,
                             const PaperPropPalette &palette,
                             const PaperPropMaterialSet &materials);

// Bind palette/material slots onto a scaled copy of the blueprint parts.
// Topology (ids, relative layout, slots) is unchanged across palette swaps.
[[nodiscard]] std::expected<ResolvedPaperProp, diagnostics::Error>
instantiate_paper_prop(const PaperPropBlueprint &blueprint,
                       const PaperPropPalette &palette,
                       const PaperPropMaterialSet &materials);

// Deterministic text dump for blueprint snapshot goldens.
[[nodiscard]] std::string
format_paper_prop_blueprint_snapshot(std::string_view label,
                                     const PaperPropBlueprint &blueprint,
                                     const ResolvedPaperProp &resolved);

} // namespace instantee::render_core
