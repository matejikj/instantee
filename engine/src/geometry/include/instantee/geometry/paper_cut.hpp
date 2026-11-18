#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/geometry/path.hpp>
#include <instantee/geometry/path_cache.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace instantee::geometry {

// Axis-aligned paper stock before the irregular cut. Coordinates are in the
// caller's local space (typically canvas pixels).
struct PaperCutBounds {
  float min_x = 0;
  float min_y = 0;
  float max_x = 0;
  float max_y = 0;
};

// Seeded deckle / irregular-cut knobs. Amplitude may push the silhouette
// inward or outward; the compiler clamps every sample so the safe inset
// rectangle stays fully covered by the fill (text layout must not be cut).
struct PaperCutParams {
  std::uint32_t seed = 1;
  // Peak |normal| displacement in path units. Must be finite and ≥ 0.
  float amplitude_px = 3.0f;
  // Approximate distance between independent noise samples along the edge.
  // Smaller ⇒ denser silhouette detail. Must be finite and > 0.
  float wavelength_px = 8.0f;
  // Inner margin reserved for text/content. Must be finite and ≥ amplitude_px
  // so the clamped cut cannot invade the safe rectangle.
  float safe_inset_px = 12.0f;
  // Inclusive samples per rectangle edge, corners shared. Bounded [2, 256].
  std::uint32_t samples_per_edge = 16;
};

struct PaperCutGeometry {
  // Outer irregular silhouette (closed). Also the authoritative mask boundary.
  Path2f outline{};
  // Axis-aligned region guaranteed inside the fill after clamping.
  PaperCutBounds safe_text_bounds{};
  TriangleMesh2f fill_mesh{};
  // Contour after flatten (outer ring only) for golden / mask raster hooks.
  FlattenedPath flattened{};
  std::uint32_t seed = 1;
  float amplitude_px = 0;
  float wavelength_px = 0;
  float safe_inset_px = 0;
};

[[nodiscard]] PaperCutParams paper_cut_defaults_v1() noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_cut_params(const PaperCutParams &params) noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_cut_bounds(const PaperCutBounds &bounds,
                          const PaperCutParams &params) noexcept;

// Deterministic closed path with seeded edge irregularities. Does not
// tessellate; callers that only need the outline (e.g. mask builders) can stop
// here. The outline is the single silhouette shared by fill and mask.
[[nodiscard]] std::expected<Path2f, diagnostics::Error>
build_irregular_cut_path(const PaperCutBounds &bounds,
                         const PaperCutParams &params = {});

// Compile outline + fill mesh with flatten/tessellate budget enforcement.
[[nodiscard]] std::expected<PaperCutGeometry, diagnostics::Error>
compile_paper_cut(const PaperCutBounds &bounds,
                  const PaperCutParams &params = {},
                  const FlattenLimits &flatten = {},
                  const TessellateLimits &tessellate = {});

// Content identity for PathGeometryCache warm hits across frames.
[[nodiscard]] std::string
paper_cut_content_key(const PaperCutBounds &bounds,
                      const PaperCutParams &params,
                      float flatten_tolerance) noexcept;

// Warm-path helper: compile once under paper_cut_content_key, then reuse the
// cached fill mesh. Outline/safe bounds are rebuilt cheaply (deterministic).
[[nodiscard]] std::expected<PaperCutGeometry, diagnostics::Error>
get_or_compile_paper_cut(PathGeometryCache &cache, const PaperCutBounds &bounds,
                         const PaperCutParams &params = {},
                         const FlattenLimits &flatten = {},
                         const TessellateLimits &tessellate = {});

// Deterministic text dump for edge-geometry goldens.
[[nodiscard]] std::string
format_paper_cut_geometry_golden(std::string_view label,
                                 const PaperCutGeometry &geometry);

} // namespace instantee::geometry
