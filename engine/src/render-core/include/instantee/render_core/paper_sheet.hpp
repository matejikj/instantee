#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/paper_material.hpp>
#include <instantee/render_core/paper_notes.hpp>
#include <instantee/render_core/render_graph.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// MSL-parity paper-sheet shading (matte pulp + fiber + wash + edge rim).
// Feature / pass ids stay shared with notebook note cards — this module never
// branches on style package names.
struct PaperSheetShadingParams {
  PaperMaterialParams material = paper_material_defaults_v1();
  Rgba8 pulp_color{250, 245, 230, 255};
  Rgba8 wash_color{255, 252, 240, 255};
};

struct PaperSheetShadingPassPlan {
  std::vector<std::string> pass_keys{};
  bool paper_sheet_active = false;
};

// Builtins + paper-sheet (same registration path as note cards).
[[nodiscard]] ShaderFeatureRegistry make_paper_sheet_shader_feature_registry();

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_sheet_shading_params(const PaperSheetShadingParams &params);

[[nodiscard]] std::expected<PaperSheetShadingPassPlan, diagnostics::Error>
plan_paper_sheet_shading_passes(
    const ShaderFeatureRegistry &registry, const MaterialDesc &material,
    const std::vector<std::string> &active_feature_names);

// Deterministic unit noise in [0, 1) shared with the MSL fragment.
[[nodiscard]] float paper_sheet_unit_noise(std::uint32_t seed, std::uint32_t x,
                                           std::uint32_t y,
                                           std::uint32_t channel) noexcept;

// CPU reference for shading goldens / contract checks. Product frame loops use
// the offline MSL module — this is not a per-frame raster path.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_paper_sheet_shading_reference(std::uint32_t width_px,
                                        std::uint32_t height_px,
                                        const PaperSheetShadingParams &params);

} // namespace instantee::render_core
