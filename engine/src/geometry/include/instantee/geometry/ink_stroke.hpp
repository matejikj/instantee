#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/geometry/arc_length.hpp>
#include <instantee/geometry/path.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::geometry {

// Pencil/ink ribbon authored once as vector mesh. Draw reveal is driven by
// arc-length progress on the vertices — never by re-stamping a CPU bitmap.
struct InkStrokeStyle {
  float width_px = 4.0f;
  // Width scale at the tips (0..1]. 1 keeps constant width.
  float tip_taper = 0.35f;
  // Extra mid-stroke pressure bulge in [0, 1].
  float pressure_amp = 0.2f;
  // Procedural brush grain amount recorded for GPU shading (0..1).
  float brush_grain = 0.35f;
  std::uint32_t brush_seed = 1;
};

struct InkStrokeVertex {
  float x = 0;
  float y = 0;
  float s = 0;      // arc length along centerline
  float s_norm = 0; // s / total_length (0 when length is 0)
  float side = 0;   // -1 left / +1 right across the stroke
  float width_px = 0;
};

struct InkStrokeMesh {
  std::vector<InkStrokeVertex> vertices{};
  std::vector<std::uint32_t> indices{}; // triangle list
  float total_length = 0;
  float brush_grain = 0;
  std::uint32_t brush_seed = 1;
};

struct InkStrokeProgressSample {
  float progress = 0;
  float revealed_length = 0;
  std::size_t visible_triangles = 0;
  std::size_t visible_vertices = 0;
  float tip_x = 0;
  float tip_y = 0;
};

// Build a variable-width ribbon from a centerline. Uses arc-length for s/s_norm
// and a tip-taper + mid pressure envelope for width. Rejects non-finite style
// values and degenerate polylines.
[[nodiscard]] std::expected<InkStrokeMesh, diagnostics::Error>
compile_ink_stroke(std::span<const Vec2f> centerline,
                   const InkStrokeStyle &style = {}, bool closed = false,
                   const ArcLengthLimits &limits = {});

[[nodiscard]] std::expected<InkStrokeMesh, diagnostics::Error>
compile_ink_stroke(const Path2f &path, const InkStrokeStyle &style = {},
                   const FlattenLimits &flatten = {},
                   const ArcLengthLimits &limits = {});

// Count triangles whose maximum vertex s_norm is ≤ progress. This models the
// GPU ink shader's arc-length discard without allocating a raster bitmap.
[[nodiscard]] std::expected<InkStrokeProgressSample, diagnostics::Error>
sample_ink_stroke_progress(const InkStrokeMesh &mesh, float progress);

// Deterministic text dump for stroke progress goldens.
[[nodiscard]] std::string
format_ink_stroke_progress_golden(std::string_view label,
                                  const InkStrokeMesh &mesh,
                                  std::span<const float> progresses);

} // namespace instantee::geometry
