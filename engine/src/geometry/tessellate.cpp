#include <instantee/geometry/path.hpp>

#include <tesselator.h>

#include <cstddef>
#include <vector>

namespace instantee::geometry {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "geometry", message};
}

[[nodiscard]] diagnostics::Error make_oom(std::string_view code,
                                          std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::out_of_memory,
                            diagnostics::ErrorKind::out_of_memory, code,
                            "geometry", message};
}

[[nodiscard]] diagnostics::Error make_unexpected(std::string_view code,
                                                 std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::unexpected_exception,
                            diagnostics::ErrorKind::unexpected_exception, code,
                            "geometry", message};
}

} // namespace

std::expected<TriangleMesh2f, diagnostics::Error>
tessellate_fill(const FlattenedPath &path, FillRule fill_rule,
                const TessellateLimits &limits) {
  if (path.contours.empty()) {
    return TriangleMesh2f{};
  }

  TESStesselator *tess = tessNewTess(nullptr);
  if (tess == nullptr) {
    return std::unexpected(make_oom("geometry.tess.oom", "tessNewTess failed"));
  }

  struct TessGuard {
    TESStesselator *ptr;
    ~TessGuard() {
      if (ptr != nullptr) {
        tessDeleteTess(ptr);
      }
    }
  } guard{tess};

  bool any = false;
  for (const auto &contour : path.contours) {
    if (contour.points.size() < 3) {
      continue;
    }
    std::vector<TESSreal> coords;
    coords.reserve(contour.points.size() * 2);
    for (const auto &p : contour.points) {
      coords.push_back(static_cast<TESSreal>(p.x));
      coords.push_back(static_cast<TESSreal>(p.y));
    }
    tessAddContour(tess, 2, coords.data(),
                   static_cast<int>(sizeof(TESSreal) * 2),
                   static_cast<int>(contour.points.size()));
    any = true;
  }
  if (!any) {
    return TriangleMesh2f{};
  }

  const int winding = (fill_rule == FillRule::even_odd) ? TESS_WINDING_ODD
                                                        : TESS_WINDING_NONZERO;
  if (tessTesselate(tess, winding, TESS_POLYGONS, 3, 2, nullptr) != 1) {
    return std::unexpected(make_invalid(
        "geometry.tess.failed", "tessTesselate failed (degenerate input?)"));
  }

  const int vertex_count = tessGetVertexCount(tess);
  const int element_count = tessGetElementCount(tess);
  const TESSreal *verts = tessGetVertices(tess);
  const TESSindex *elems = tessGetElements(tess);
  if (verts == nullptr || elems == nullptr || vertex_count < 0 ||
      element_count < 0) {
    return std::unexpected(make_unexpected(
        "geometry.tess.failed", "tessellator returned null buffers"));
  }

  if (static_cast<std::uint32_t>(element_count) > limits.max_triangles) {
    return std::unexpected(
        make_invalid("geometry.tess.limit", "triangle budget exceeded"));
  }

  TriangleMesh2f mesh;
  mesh.vertices.reserve(static_cast<std::size_t>(vertex_count));
  for (int i = 0; i < vertex_count; ++i) {
    mesh.vertices.push_back(Vec2f{static_cast<float>(verts[i * 2 + 0]),
                                  static_cast<float>(verts[i * 2 + 1])});
  }
  mesh.indices.reserve(static_cast<std::size_t>(element_count) * 3);
  for (int i = 0; i < element_count; ++i) {
    const TESSindex i0 = elems[i * 3 + 0];
    const TESSindex i1 = elems[i * 3 + 1];
    const TESSindex i2 = elems[i * 3 + 2];
    if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
      continue;
    }
    mesh.indices.push_back(static_cast<std::uint32_t>(i0));
    mesh.indices.push_back(static_cast<std::uint32_t>(i1));
    mesh.indices.push_back(static_cast<std::uint32_t>(i2));
  }
  return mesh;
}

} // namespace instantee::geometry
