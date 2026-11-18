#include <instantee/geometry/ink_stroke.hpp>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::geometry {
namespace {

diagnostics::Error make_invalid(std::string_view code,
                                std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "geometry", message};
}

[[nodiscard]] float clamp01(float v) noexcept {
  if (!(v > 0.0f)) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

[[nodiscard]] Vec2f normalized(Vec2f v) noexcept {
  const float len = std::sqrt(v.x * v.x + v.y * v.y);
  if (!(len > 1e-8f)) {
    return Vec2f{1.0f, 0.0f};
  }
  return Vec2f{v.x / len, v.y / len};
}

[[nodiscard]] Vec2f perp(Vec2f v) noexcept { return Vec2f{-v.y, v.x}; }

[[nodiscard]] float smoothstep(float edge0, float edge1, float x) noexcept {
  if (!(edge1 > edge0)) {
    return x >= edge1 ? 1.0f : 0.0f;
  }
  const float t = clamp01((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] float width_scale_at(float s_norm, float tip_taper,
                                   float pressure_amp) noexcept {
  const float taper_zone = 0.18f;
  const float from_start = smoothstep(0.0f, taper_zone, s_norm);
  const float from_end = smoothstep(0.0f, taper_zone, 1.0f - s_norm);
  const float envelope = from_start * from_end;
  const float tapered = tip_taper + (1.0f - tip_taper) * envelope;
  const float bulge =
      1.0f + pressure_amp * std::sin(3.14159265f * clamp01(s_norm));
  return tapered * bulge;
}

[[nodiscard]] bool finite_style(const InkStrokeStyle &style) noexcept {
  return std::isfinite(style.width_px) && style.width_px > 0.0f &&
         std::isfinite(style.tip_taper) && style.tip_taper > 0.0f &&
         style.tip_taper <= 1.0f && std::isfinite(style.pressure_amp) &&
         style.pressure_amp >= 0.0f && style.pressure_amp <= 1.0f &&
         std::isfinite(style.brush_grain) && style.brush_grain >= 0.0f &&
         style.brush_grain <= 1.0f;
}

} // namespace

std::expected<InkStrokeMesh, diagnostics::Error>
compile_ink_stroke(std::span<const Vec2f> centerline,
                   const InkStrokeStyle &style, bool closed,
                   const ArcLengthLimits &limits) {
  if (!finite_style(style)) {
    return std::unexpected(make_invalid(
        "geometry.ink_stroke.invalid",
        "ink stroke style must be finite with positive width/taper"));
  }

  auto table = build_arc_length_table(centerline, closed, limits);
  if (!table) {
    return std::unexpected(table.error());
  }
  if (table->points.size() < 2) {
    return std::unexpected(make_invalid(
        "geometry.ink_stroke.invalid", "ink stroke needs a usable centerline"));
  }

  const std::size_t n = table->points.size();
  const float total = table->total_length;
  InkStrokeMesh mesh;
  mesh.total_length = total;
  mesh.brush_grain = style.brush_grain;
  mesh.brush_seed = style.brush_seed;
  mesh.vertices.reserve(n * 2);
  mesh.indices.reserve((closed ? n : n - 1) * 6);

  for (std::size_t i = 0; i < n; ++i) {
    Vec2f tangent{};
    if (closed) {
      const Vec2f prev = table->points[(i + n - 1) % n];
      const Vec2f next = table->points[(i + 1) % n];
      tangent = normalized(Vec2f{next.x - prev.x, next.y - prev.y});
    } else if (i == 0) {
      tangent = normalized(Vec2f{table->points[1].x - table->points[0].x,
                                 table->points[1].y - table->points[0].y});
    } else if (i + 1 == n) {
      tangent =
          normalized(Vec2f{table->points[n - 1].x - table->points[n - 2].x,
                           table->points[n - 1].y - table->points[n - 2].y});
    } else {
      tangent =
          normalized(Vec2f{table->points[i + 1].x - table->points[i - 1].x,
                           table->points[i + 1].y - table->points[i - 1].y});
    }
    const Vec2f normal = perp(tangent);
    const float s = table->cumulative_s[i];
    const float s_norm = total > 0.0f ? s / total : 0.0f;
    const float width = style.width_px * width_scale_at(s_norm, style.tip_taper,
                                                        style.pressure_amp);
    const float half = width * 0.5f;
    const Vec2f c = table->points[i];

    InkStrokeVertex left{
        .x = c.x - normal.x * half,
        .y = c.y - normal.y * half,
        .s = s,
        .s_norm = s_norm,
        .side = -1.0f,
        .width_px = width,
    };
    InkStrokeVertex right{
        .x = c.x + normal.x * half,
        .y = c.y + normal.y * half,
        .s = s,
        .s_norm = s_norm,
        .side = 1.0f,
        .width_px = width,
    };
    mesh.vertices.push_back(left);
    mesh.vertices.push_back(right);
  }

  auto push_quad = [&](std::uint32_t i0, std::uint32_t i1) {
    // i0/i1 are centerline indices; verts are 2*i and 2*i+1.
    const std::uint32_t a = i0 * 2u;
    const std::uint32_t b = i0 * 2u + 1u;
    const std::uint32_t c = i1 * 2u + 1u;
    const std::uint32_t d = i1 * 2u;
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
    mesh.indices.push_back(a);
    mesh.indices.push_back(c);
    mesh.indices.push_back(d);
  };

  if (closed) {
    for (std::size_t i = 0; i < n; ++i) {
      push_quad(static_cast<std::uint32_t>(i),
                static_cast<std::uint32_t>((i + 1) % n));
    }
  } else {
    for (std::size_t i = 0; i + 1 < n; ++i) {
      push_quad(static_cast<std::uint32_t>(i),
                static_cast<std::uint32_t>(i + 1));
    }
  }

  return mesh;
}

std::expected<InkStrokeMesh, diagnostics::Error>
compile_ink_stroke(const Path2f &path, const InkStrokeStyle &style,
                   const FlattenLimits &flatten,
                   const ArcLengthLimits &limits) {
  auto table = build_arc_length_table(path, flatten, limits);
  if (!table) {
    return std::unexpected(table.error());
  }
  return compile_ink_stroke(std::span<const Vec2f>{table->points}, style,
                            table->closed, limits);
}

std::expected<InkStrokeProgressSample, diagnostics::Error>
sample_ink_stroke_progress(const InkStrokeMesh &mesh, float progress) {
  if (!std::isfinite(progress)) {
    return std::unexpected(make_invalid("geometry.ink_stroke.bad_progress",
                                        "progress must be finite"));
  }
  if (mesh.indices.size() % 3u != 0u) {
    return std::unexpected(make_invalid("geometry.ink_stroke.invalid",
                                        "ink mesh indices must be triangles"));
  }

  const float u = clamp01(progress);
  InkStrokeProgressSample sample;
  sample.progress = u;
  sample.revealed_length = u * mesh.total_length;

  std::vector<char> vert_visible(mesh.vertices.size(), 0);
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    if (mesh.vertices[i].s_norm <= u + 1e-6f) {
      vert_visible[i] = 1;
      ++sample.visible_vertices;
    }
  }

  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const auto a = mesh.indices[i];
    const auto b = mesh.indices[i + 1];
    const auto c = mesh.indices[i + 2];
    if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
        c >= mesh.vertices.size()) {
      return std::unexpected(make_invalid("geometry.ink_stroke.invalid",
                                          "ink mesh index out of range"));
    }
    // A triangle becomes visible once all of its vertices have been revealed,
    // matching a hard arc-length discard in the fragment shader.
    const float smax =
        std::max({mesh.vertices[a].s_norm, mesh.vertices[b].s_norm,
                  mesh.vertices[c].s_norm});
    if (smax <= u + 1e-6f) {
      ++sample.visible_triangles;
    }
  }

  // Tip = centerline point at revealed length (average of left/right verts).
  const std::size_t pairs = mesh.vertices.size() / 2;
  if (pairs == 0) {
    return sample;
  }
  if (u <= 0.0f || !(mesh.total_length > 0.0f)) {
    const auto &L = mesh.vertices[0];
    const auto &R = mesh.vertices[1];
    sample.tip_x = (L.x + R.x) * 0.5f;
    sample.tip_y = (L.y + R.y) * 0.5f;
    return sample;
  }
  if (u >= 1.0f) {
    const auto &L = mesh.vertices[(pairs - 1) * 2];
    const auto &R = mesh.vertices[(pairs - 1) * 2 + 1];
    sample.tip_x = (L.x + R.x) * 0.5f;
    sample.tip_y = (L.y + R.y) * 0.5f;
    return sample;
  }
  const float target = u * mesh.total_length;
  for (std::size_t i = 0; i + 1 < pairs; ++i) {
    const auto &L0 = mesh.vertices[i * 2];
    const auto &R0 = mesh.vertices[i * 2 + 1];
    const auto &L1 = mesh.vertices[(i + 1) * 2];
    const auto &R1 = mesh.vertices[(i + 1) * 2 + 1];
    if (target > L1.s) {
      continue;
    }
    const float seg = L1.s - L0.s;
    const float t = seg > 0.0f ? (target - L0.s) / seg : 0.0f;
    const float x0 = (L0.x + R0.x) * 0.5f;
    const float y0 = (L0.y + R0.y) * 0.5f;
    const float x1 = (L1.x + R1.x) * 0.5f;
    const float y1 = (L1.y + R1.y) * 0.5f;
    sample.tip_x = x0 + (x1 - x0) * t;
    sample.tip_y = y0 + (y1 - y0) * t;
    break;
  }
  return sample;
}

std::string
format_ink_stroke_progress_golden(std::string_view label,
                                  const InkStrokeMesh &mesh,
                                  std::span<const float> progresses) {
  std::ostringstream oss;
  oss << "ink_stroke " << label << '\n';
  oss << std::fixed << std::setprecision(4);
  oss << "verts=" << mesh.vertices.size() << " tris=" << mesh.indices.size() / 3
      << " length=" << mesh.total_length << " grain=" << mesh.brush_grain
      << " seed=" << mesh.brush_seed << '\n';

  // Sample a few vertices for width envelope stability (deduped indices).
  const std::size_t pairs = mesh.vertices.size() / 2;
  std::vector<std::size_t> picks{0, pairs / 4, pairs / 2};
  if (pairs > 0) {
    picks.push_back(pairs - 1);
  }
  std::size_t prev = static_cast<std::size_t>(-1);
  for (std::size_t pi : picks) {
    if (pi >= pairs || pi == prev) {
      continue;
    }
    prev = pi;
    const auto &v = mesh.vertices[pi * 2];
    oss << "w@" << v.s_norm << "=" << v.width_px << '\n';
  }

  for (float p : progresses) {
    auto sample = sample_ink_stroke_progress(mesh, p);
    if (!sample) {
      oss << "progress=" << p << " error=" << sample.error().code << '\n';
      continue;
    }
    oss << "progress=" << sample->progress
        << " revealed=" << sample->revealed_length
        << " tris=" << sample->visible_triangles
        << " verts=" << sample->visible_vertices << " tip=" << sample->tip_x
        << "," << sample->tip_y << '\n';
  }
  return oss.str();
}

} // namespace instantee::geometry
