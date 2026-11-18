#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/paper_notes.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::render_core {

// Versioned Paper Cinema / shared paper-sheet material knobs. Styles supply
// these as numeric parameters; render-core never branches on package names.
// Model version bumps when defaults or allowed ranges change.
inline constexpr std::string_view kPaperMaterialModelVersion = "1.0.0";

// Canonical parameter ids accepted from Style Bible material.parameters maps.
inline constexpr std::string_view kPaperParamFiber = "fiber";
inline constexpr std::string_view kPaperParamRoughness = "roughness";
inline constexpr std::string_view kPaperParamColorWash = "color_wash";
inline constexpr std::string_view kPaperParamEdgeThickness = "edge_thickness";
inline constexpr std::string_view kPaperParamShadowBias = "shadow_bias";
inline constexpr std::string_view kPaperParamNormalVariation =
    "normal_variation";
inline constexpr std::string_view kPaperParamSeed = "seed";

struct PaperMaterialRanges {
  float fiber_min = 0.0f;
  float fiber_max = 1.0f;
  float roughness_min = 0.0f;
  float roughness_max = 1.0f;
  float color_wash_min = 0.0f;
  float color_wash_max = 1.0f;
  float edge_thickness_min = 0.0f;
  float edge_thickness_max = 1.0f;
  float shadow_bias_min = 0.0f;
  float shadow_bias_max = 1.0f;
  float normal_variation_min = 0.0f;
  float normal_variation_max = 1.0f;
  // Seed is a non-negative finite integer that fits in uint32 for GPU packing.
  double seed_min = 0.0;
  double seed_max = 4294967295.0;
};

struct PaperMaterialParams {
  float fiber = 0.4f;
  float roughness = 0.35f;
  float color_wash = 0.12f;
  float edge_thickness = 0.04f;
  float shadow_bias = 0.06f;
  float normal_variation = 0.15f;
  std::uint32_t seed = 1;
};

[[nodiscard]] PaperMaterialRanges paper_material_ranges_v1() noexcept;

[[nodiscard]] PaperMaterialParams paper_material_defaults_v1() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_material_params(
    const PaperMaterialParams &params,
    const PaperMaterialRanges &ranges = paper_material_ranges_v1());

// Merge an authored parameter map onto versioned defaults. Unknown keys and
// non-finite / out-of-range values fail closed. Omitted keys keep defaults.
[[nodiscard]] std::expected<PaperMaterialParams, diagnostics::Error>
parse_paper_material_params(
    const std::unordered_map<std::string, double> &authored,
    const PaperMaterialParams &defaults = paper_material_defaults_v1(),
    const PaperMaterialRanges &ranges = paper_material_ranges_v1());

// Register paper-sheet feature parameters from the versioned defaults so
// inactive/active planning stays shared with notebook note cards.
[[nodiscard]] std::vector<ShaderParamDesc> paper_material_shader_param_descs(
    const PaperMaterialParams &defaults = paper_material_defaults_v1());

// Map normalized Cinema knobs onto the CPU note-card paper sheet fields used by
// the existing DrawOps path (edge thickness → px via a fixed scale).
[[nodiscard]] PaperSheetParams
paper_material_to_sheet_params(const PaperMaterialParams &params,
                               float edge_thickness_px_scale = 32.0f) noexcept;

} // namespace instantee::render_core
