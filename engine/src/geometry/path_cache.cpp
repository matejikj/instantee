#include <instantee/geometry/path_cache.hpp>

namespace instantee::geometry {

std::expected<const TriangleMesh2f *, diagnostics::Error>
PathGeometryCache::get_or_compile_fill(const Path2f &path, FillRule fill_rule,
                                       const FlattenLimits &flatten,
                                       const TessellateLimits &limits) {
  const std::string key = path_content_key(path, fill_rule, flatten.tolerance);
  if (const auto it = meshes_.find(key); it != meshes_.end()) {
    ++stats_.hits;
    return &it->second;
  }

  ++stats_.misses;
  auto flat = flatten_path(path, flatten);
  if (!flat) {
    return std::unexpected(flat.error());
  }
  auto mesh = tessellate_fill(*flat, fill_rule, limits);
  if (!mesh) {
    return std::unexpected(mesh.error());
  }
  ++stats_.compiles;
  auto [it, inserted] = meshes_.emplace(key, std::move(*mesh));
  (void)inserted;
  return &it->second;
}

std::expected<const TriangleMesh2f *, diagnostics::Error>
PathGeometryCache::get_or_compile_stroke(const Path2f &path,
                                         const StrokeStyle &style,
                                         const FlattenLimits &flatten,
                                         const TessellateLimits &limits) {
  const std::string key = stroke_content_key(path, style, flatten.tolerance);
  if (const auto it = meshes_.find(key); it != meshes_.end()) {
    ++stats_.hits;
    return &it->second;
  }

  ++stats_.misses;
  auto mesh = stroke_path(path, style, flatten, limits);
  if (!mesh) {
    return std::unexpected(mesh.error());
  }
  ++stats_.compiles;
  auto [it, inserted] = meshes_.emplace(key, std::move(*mesh));
  (void)inserted;
  return &it->second;
}

void PathGeometryCache::clear() noexcept {
  meshes_.clear();
  stats_ = {};
}

} // namespace instantee::geometry
