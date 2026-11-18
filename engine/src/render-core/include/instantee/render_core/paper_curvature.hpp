#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::render_core {

// Light paper curvature: parametric bend / wave applied to a regular grid.
// Operations are bounded by an explicit GPU complexity tier. This module is
// CPU-reference + feature registration; backends may mirror the same op budget
// in a vertex stage without inventing extra passes.

inline constexpr std::string_view kPaperCurvatureFeatureName =
    "paper_curvature";
inline constexpr std::string_view kPaperCurvatureTierId =
    "paper-curvature-lite-v1";

enum class PaperCurvatureMode : std::uint8_t {
  bend = 1, // cylindrical bend about the sheet mid-Y axis
  wave = 2, // sinusoidal displacement along +Z (camera-forward −Z uses −amp)
};

struct PaperCurvatureTier {
  std::string id = std::string(kPaperCurvatureTierId);
  std::uint32_t max_grid_u = 32; // cells
  std::uint32_t max_grid_v = 32;
  std::uint32_t max_ops_per_vertex = 8;
  std::uint64_t max_total_ops = 32ULL * 32ULL * 8ULL;
};

struct PaperCurvatureParams {
  PaperCurvatureMode mode = PaperCurvatureMode::bend;
  // Bend: total arc across sheet width. 0 = flat.
  double bend_angle_rad = 0;
  // Wave: z' = z + amp * sin(2π (freq_u * u + freq_v * v + phase))
  double wave_amp_px = 0;
  double wave_freq_u = 1.0;
  double wave_freq_v = 0.0;
  double wave_phase = 0.0;
};

struct PaperCurvatureMesh {
  std::string id;
  double width = 1;
  double height = 1;
  std::uint32_t grid_u = 1; // cells in X
  std::uint32_t grid_v = 1; // cells in Y
  std::vector<core::Vec3> bind_vertices;
  std::vector<std::uint32_t> indices;
};

struct PaperCurvaturePose {
  std::vector<core::Vec3> vertices;
  std::vector<std::uint32_t> indices;
  std::uint64_t ops_used = 0;
  PaperCurvatureTier tier{};
};

struct PaperCurvatureSilhouetteReport {
  bool watertight = false;
  bool no_collapsed_edges = false;
  bool within_tier = false;
  double min_edge_length = 0;
  std::uint64_t shared_edge_mismatches = 0;
  std::uint64_t ops_used = 0;
  std::uint64_t ops_budget = 0;
};

[[nodiscard]] PaperCurvatureTier paper_curvature_tier_lite_v1() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_curvature_tier(const PaperCurvatureTier &tier);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_curvature_params(const PaperCurvatureParams &params);

[[nodiscard]] std::expected<PaperCurvatureMesh, diagnostics::Error>
make_paper_curvature_grid(
    double width, double height, std::uint32_t grid_u, std::uint32_t grid_v,
    const PaperCurvatureTier &tier = paper_curvature_tier_lite_v1());

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_curvature_mesh(
    const PaperCurvatureMesh &mesh,
    const PaperCurvatureTier &tier = paper_curvature_tier_lite_v1());

// Estimated ALU ops charged per vertex for the mode (tier accounting).
[[nodiscard]] std::uint32_t
paper_curvature_ops_per_vertex(PaperCurvatureMode mode) noexcept;

[[nodiscard]] std::expected<PaperCurvaturePose, diagnostics::Error>
evaluate_paper_curvature(
    const PaperCurvatureMesh &mesh, const PaperCurvatureParams &params,
    const PaperCurvatureTier &tier = paper_curvature_tier_lite_v1());

[[nodiscard]] std::expected<PaperCurvatureSilhouetteReport, diagnostics::Error>
inspect_paper_curvature_silhouette(const PaperCurvatureMesh &mesh,
                                   const PaperCurvaturePose &pose);

// Registers paper_curvature feature (one documented vertex-deform pass key).
[[nodiscard]] std::expected<void, diagnostics::Error>
register_paper_curvature_feature(ShaderFeatureRegistry &registry);

[[nodiscard]] std::string format_paper_curvature_golden(
    std::string_view label, const PaperCurvatureMesh &mesh,
    const PaperCurvaturePose &pose, const PaperCurvatureParams &params);

} // namespace instantee::render_core
