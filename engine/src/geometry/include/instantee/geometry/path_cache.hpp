#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/geometry/path.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>

namespace instantee::geometry {

struct PathCacheStats {
  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t compiles = 0;
};

// Compile-once mesh cache for static path geometry. A repeated lookup with the
// same content key never re-runs flatten/tessellate/stroke.
class PathGeometryCache {
public:
  [[nodiscard]] const PathCacheStats &stats() const noexcept { return stats_; }
  [[nodiscard]] std::size_t size() const noexcept { return meshes_.size(); }

  [[nodiscard]] std::expected<const TriangleMesh2f *, diagnostics::Error>
  get_or_compile_fill(const Path2f &path, FillRule fill_rule,
                      const FlattenLimits &flatten = {},
                      const TessellateLimits &limits = {});

  [[nodiscard]] std::expected<const TriangleMesh2f *, diagnostics::Error>
  get_or_compile_stroke(const Path2f &path, const StrokeStyle &style,
                        const FlattenLimits &flatten = {},
                        const TessellateLimits &limits = {});

  void clear() noexcept;

private:
  PathCacheStats stats_{};
  std::unordered_map<std::string, TriangleMesh2f> meshes_{};
};

} // namespace instantee::geometry
