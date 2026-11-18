#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/geometry/path.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace instantee::geometry {

// Precomputed arc-length parameterization of a polyline. cumulative_s[i] is the
// path length from points[0] to points[i]. Draw progress must sample this table
// (not vertex indices) so uneven segment spacing stays temporally correct.
struct ArcLengthTable {
  std::vector<Vec2f> points{};
  std::vector<float> cumulative_s{}; // same size as points; [0] == 0
  float total_length = 0.0f;
  bool closed = false;
};

struct ArcLengthLimits {
  std::uint32_t max_points = 16384;
};

// Build a table from an explicit polyline. Duplicate consecutive points are
// collapsed. Closed polylines include the closing segment in total_length.
// Rejects empty input, non-finite coordinates, and budgets over max_points.
[[nodiscard]] std::expected<ArcLengthTable, diagnostics::Error>
build_arc_length_table(std::span<const Vec2f> points, bool closed = false,
                       const ArcLengthLimits &limits = {});

// Flatten a path, then build a table from its first contour that has at least
// two points. Contour closure follows PathVerb::close during flatten.
[[nodiscard]] std::expected<ArcLengthTable, diagnostics::Error>
build_arc_length_table(const Path2f &path, const FlattenLimits &flatten = {},
                       const ArcLengthLimits &limits = {});

// Point at absolute arc distance along the table. Distance is clamped to
// [0, total_length]. Zero-length tables return the sole remaining point.
[[nodiscard]] std::expected<Vec2f, diagnostics::Error>
sample_at_arc_length(const ArcLengthTable &table, float distance);

// Point at normalized progress u ∈ [0, 1] by arc length (not vertex index).
// Non-finite u is rejected; out-of-range u is clamped.
[[nodiscard]] std::expected<Vec2f, diagnostics::Error>
sample_at_progress(const ArcLengthTable &table, float progress);

// Polyline prefix covering [0, progress] of total length, including an
// interpolated endpoint. Useful for stroke reveal / draw-on meshes.
[[nodiscard]] std::expected<std::vector<Vec2f>, diagnostics::Error>
prefix_at_progress(const ArcLengthTable &table, float progress);

} // namespace instantee::geometry
