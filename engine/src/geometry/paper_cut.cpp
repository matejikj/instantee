#include <instantee/geometry/paper_cut.hpp>

#include <algorithm>
#include <cmath>
#include <format>
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

[[nodiscard]] std::uint32_t mix_u32(std::uint32_t a, std::uint32_t b) noexcept {
  std::uint32_t x = a ^ (b + 0x9E3779B9u + (a << 6u) + (a >> 2u));
  x ^= x >> 16u;
  x *= 0x7FEB352Du;
  x ^= x >> 15u;
  x *= 0x846CA68Bu;
  x ^= x >> 16u;
  return x;
}

[[nodiscard]] float unit_from_hash(std::uint32_t h) noexcept {
  return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

// Signed edge noise in [-1, 1], stable for a given (seed, sample index).
[[nodiscard]] float signed_edge_noise(std::uint32_t seed,
                                      std::uint32_t sample) noexcept {
  const float u = unit_from_hash(mix_u32(seed ^ 0xC07Cu, sample ^ 0xE001u));
  return u * 2.0f - 1.0f;
}

[[nodiscard]] bool finite_nonneg(float v) noexcept {
  return std::isfinite(v) && v >= 0.0f;
}

[[nodiscard]] PaperCutBounds inset_bounds(const PaperCutBounds &bounds,
                                          float inset) noexcept {
  return PaperCutBounds{.min_x = bounds.min_x + inset,
                        .min_y = bounds.min_y + inset,
                        .max_x = bounds.max_x - inset,
                        .max_y = bounds.max_y - inset};
}

// Push a sample along the outward normal, then clamp so it cannot enter the
// safe text rectangle (covers both inward cuts and outward deckle growth).
[[nodiscard]] Vec2f displace_and_clamp(Vec2f base, Vec2f outward_n,
                                       float offset,
                                       const PaperCutBounds &safe) noexcept {
  Vec2f p{base.x + outward_n.x * offset, base.y + outward_n.y * offset};

  // Keep the silhouette outside the safe rect: if a point landed inside, snap
  // it onto the nearest safe edge along the edge normal (axis-aligned edges).
  const bool inside = p.x > safe.min_x && p.x < safe.max_x &&
                      p.y > safe.min_y && p.y < safe.max_y;
  if (!inside) {
    return p;
  }

  if (std::fabs(outward_n.x) >= std::fabs(outward_n.y)) {
    p.x = outward_n.x >= 0.0f ? safe.max_x : safe.min_x;
    p.y = base.y;
  } else {
    p.y = outward_n.y >= 0.0f ? safe.max_y : safe.min_y;
    p.x = base.x;
  }
  return p;
}

[[nodiscard]] std::expected<std::vector<Vec2f>, diagnostics::Error>
sample_irregular_ring(const PaperCutBounds &bounds,
                      const PaperCutParams &params) {
  const float width = bounds.max_x - bounds.min_x;
  const float height = bounds.max_y - bounds.min_y;
  const auto safe = inset_bounds(bounds, params.safe_inset_px);

  // Derive samples from wavelength, then clamp to the authored ceiling so
  // denser edges stay inside FlattenLimits without surprising blow-ups.
  const float perimeter = 2.0f * (width + height);
  const float step = std::max(params.wavelength_px, 1e-3f);
  const auto from_wavelength = static_cast<std::uint32_t>(
      std::clamp(std::ceil(perimeter / step), 8.0f, 1024.0f));
  // Distribute samples across four edges; keep authored samples_per_edge as
  // the primary density control when wavelength would under-sample.
  const std::uint32_t per_edge = std::max(
      params.samples_per_edge, std::max(2u, (from_wavelength + 3u) / 4u));
  if (per_edge > 256u) {
    return std::unexpected(make_invalid(
        "geometry.paper_cut.limit",
        "irregular-cut sample budget exceeded (max 256 per edge)"));
  }

  std::vector<Vec2f> ring;
  ring.reserve(static_cast<std::size_t>(per_edge) * 4u);
  std::uint32_t sample_i = 0;

  auto append_edge = [&](Vec2f a, Vec2f b, Vec2f outward_n) {
    // Skip the end corner; the next edge owns it (closed ring).
    for (std::uint32_t i = 0; i < per_edge; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(per_edge);
      const Vec2f base{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
      const float offset =
          signed_edge_noise(params.seed, sample_i++) * params.amplitude_px;
      ring.push_back(displace_and_clamp(base, outward_n, offset, safe));
    }
  };

  // CCW: bottom → right → top → left. Outward normals point outside the stock.
  append_edge({bounds.min_x, bounds.min_y}, {bounds.max_x, bounds.min_y},
              {0.0f, -1.0f});
  append_edge({bounds.max_x, bounds.min_y}, {bounds.max_x, bounds.max_y},
              {1.0f, 0.0f});
  append_edge({bounds.max_x, bounds.max_y}, {bounds.min_x, bounds.max_y},
              {0.0f, 1.0f});
  append_edge({bounds.min_x, bounds.max_y}, {bounds.min_x, bounds.min_y},
              {-1.0f, 0.0f});

  if (ring.size() < 4) {
    return std::unexpected(make_invalid("geometry.paper_cut.invalid",
                                        "irregular-cut ring is degenerate"));
  }
  return ring;
}

[[nodiscard]] Path2f path_from_ring(const std::vector<Vec2f> &ring) {
  Path2f path;
  path.commands.reserve(ring.size() + 2);
  path.commands.push_back(
      PathCommand{.verb = PathVerb::move_to, .p0 = ring.front()});
  for (std::size_t i = 1; i < ring.size(); ++i) {
    path.commands.push_back(
        PathCommand{.verb = PathVerb::line_to, .p0 = ring[i]});
  }
  path.commands.push_back(PathCommand{.verb = PathVerb::close});
  return path;
}

// Ray-crossing fill test against the closed ring (even-odd). Used to assert
// the safe-text rectangle remains covered after clamping.
[[nodiscard]] bool point_in_ring(Vec2f p, const std::vector<Vec2f> &ring) {
  bool inside = false;
  const std::size_t n = ring.size();
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Vec2f a = ring[j];
    const Vec2f b = ring[i];
    const bool straddles =
        ((a.y > p.y) != (b.y > p.y)) &&
        (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) + 0.0f) + a.x);
    if (straddles) {
      inside = !inside;
    }
  }
  return inside;
}

[[nodiscard]] std::uint64_t quantize_q(float v) noexcept {
  // Fixed-point micro-units keep goldens stable across float formatting.
  return static_cast<std::uint64_t>(
      std::llround(static_cast<double>(v) * 1000.0));
}

[[nodiscard]] std::uint64_t mesh_checksum(const TriangleMesh2f &mesh) noexcept {
  std::uint64_t h = 1469598103934665603ull;
  auto mix = [&](std::uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  mix(mesh.vertices.size());
  mix(mesh.indices.size());
  for (const auto &v : mesh.vertices) {
    mix(quantize_q(v.x));
    mix(quantize_q(v.y));
  }
  for (const auto idx : mesh.indices) {
    mix(idx);
  }
  return h;
}

} // namespace

PaperCutParams paper_cut_defaults_v1() noexcept { return PaperCutParams{}; }

std::expected<void, diagnostics::Error>
validate_paper_cut_params(const PaperCutParams &params) noexcept {
  if (!finite_nonneg(params.amplitude_px) ||
      !std::isfinite(params.wavelength_px) || !(params.wavelength_px > 0.0f) ||
      !finite_nonneg(params.safe_inset_px)) {
    return std::unexpected(make_invalid(
        "geometry.paper_cut.invalid",
        "paper-cut params must be finite with positive wavelength"));
  }
  if (params.amplitude_px > params.safe_inset_px) {
    return std::unexpected(make_invalid(
        "geometry.paper_cut.unsafe",
        "amplitude_px must be ≤ safe_inset_px so text regions stay covered"));
  }
  if (params.samples_per_edge < 2u || params.samples_per_edge > 256u) {
    return std::unexpected(make_invalid(
        "geometry.paper_cut.invalid", "samples_per_edge must be in [2, 256]"));
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_paper_cut_bounds(const PaperCutBounds &bounds,
                          const PaperCutParams &params) noexcept {
  if (!std::isfinite(bounds.min_x) || !std::isfinite(bounds.min_y) ||
      !std::isfinite(bounds.max_x) || !std::isfinite(bounds.max_y)) {
    return std::unexpected(make_invalid("geometry.paper_cut.invalid",
                                        "paper-cut bounds must be finite"));
  }
  const float width = bounds.max_x - bounds.min_x;
  const float height = bounds.max_y - bounds.min_y;
  if (!(width > 0.0f) || !(height > 0.0f)) {
    return std::unexpected(make_invalid("geometry.paper_cut.invalid",
                                        "paper-cut bounds must be non-empty"));
  }
  const float min_span = 2.0f * params.safe_inset_px + 1e-3f;
  if (width < min_span || height < min_span) {
    return std::unexpected(make_invalid(
        "geometry.paper_cut.unsafe",
        "bounds too small for safe_inset_px (no room for text region)"));
  }
  return {};
}

[[nodiscard]] std::expected<std::vector<Vec2f>, diagnostics::Error>
validated_ring(const PaperCutBounds &bounds, const PaperCutParams &params) {
  if (auto ok = validate_paper_cut_params(params); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_paper_cut_bounds(bounds, params); !ok) {
    return std::unexpected(ok.error());
  }
  auto ring = sample_irregular_ring(bounds, params);
  if (!ring) {
    return std::unexpected(ring.error());
  }

  const auto safe = inset_bounds(bounds, params.safe_inset_px);
  // Probe slightly inside the safe rect so boundary ray-cast ambiguity cannot
  // false-fail when amplitude saturates the inset.
  constexpr float kProbe = 0.5f;
  const float x0 = safe.min_x + kProbe;
  const float y0 = safe.min_y + kProbe;
  const float x1 = safe.max_x - kProbe;
  const float y1 = safe.max_y - kProbe;
  if (!(x1 > x0) || !(y1 > y0)) {
    return std::unexpected(
        make_invalid("geometry.paper_cut.unsafe",
                     "safe text region too small to verify coverage"));
  }
  const Vec2f probes[] = {{x0, y0},
                          {x1, y0},
                          {x1, y1},
                          {x0, y1},
                          {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f}};
  for (const auto &c : probes) {
    if (!point_in_ring(c, *ring)) {
      return std::unexpected(make_invalid(
          "geometry.paper_cut.unsafe",
          "safe text region is not covered by the irregular-cut silhouette"));
    }
  }
  for (const auto &p : *ring) {
    const bool strictly_inside = p.x > safe.min_x && p.x < safe.max_x &&
                                 p.y > safe.min_y && p.y < safe.max_y;
    if (strictly_inside) {
      return std::unexpected(
          make_invalid("geometry.paper_cut.unsafe",
                       "cut vertex invaded the safe text region"));
    }
  }
  return ring;
}

[[nodiscard]] PaperCutGeometry make_geometry_shell(const PaperCutBounds &bounds,
                                                   const PaperCutParams &params,
                                                   Path2f outline,
                                                   FlattenedPath flat) {
  PaperCutGeometry out;
  out.outline = std::move(outline);
  out.safe_text_bounds = inset_bounds(bounds, params.safe_inset_px);
  out.flattened = std::move(flat);
  out.seed = params.seed;
  out.amplitude_px = params.amplitude_px;
  out.wavelength_px = params.wavelength_px;
  out.safe_inset_px = params.safe_inset_px;
  return out;
}

std::expected<Path2f, diagnostics::Error>
build_irregular_cut_path(const PaperCutBounds &bounds,
                         const PaperCutParams &params) {
  auto ring = validated_ring(bounds, params);
  if (!ring) {
    return std::unexpected(ring.error());
  }
  return path_from_ring(*ring);
}

std::expected<PaperCutGeometry, diagnostics::Error>
compile_paper_cut(const PaperCutBounds &bounds, const PaperCutParams &params,
                  const FlattenLimits &flatten,
                  const TessellateLimits &tessellate) {
  auto ring = validated_ring(bounds, params);
  if (!ring) {
    return std::unexpected(ring.error());
  }
  Path2f outline = path_from_ring(*ring);

  auto flat = flatten_path(outline, flatten);
  if (!flat) {
    return std::unexpected(flat.error());
  }
  auto mesh = tessellate_fill(*flat, FillRule::nonzero, tessellate);
  if (!mesh) {
    return std::unexpected(mesh.error());
  }

  auto out =
      make_geometry_shell(bounds, params, std::move(outline), std::move(*flat));
  out.fill_mesh = std::move(*mesh);
  return out;
}

std::string paper_cut_content_key(const PaperCutBounds &bounds,
                                  const PaperCutParams &params,
                                  float flatten_tolerance) noexcept {
  return std::format(
      "paper_cut|seed={}|amp={:.6g}|wave={:.6g}|inset={:.6g}|spe={}|"
      "b={:.6g},{:.6g},{:.6g},{:.6g}|tol={:.6g}",
      params.seed, params.amplitude_px, params.wavelength_px,
      params.safe_inset_px, params.samples_per_edge, bounds.min_x, bounds.min_y,
      bounds.max_x, bounds.max_y, flatten_tolerance);
}

std::expected<PaperCutGeometry, diagnostics::Error>
get_or_compile_paper_cut(PathGeometryCache &cache, const PaperCutBounds &bounds,
                         const PaperCutParams &params,
                         const FlattenLimits &flatten,
                         const TessellateLimits &tessellate) {
  auto ring = validated_ring(bounds, params);
  if (!ring) {
    return std::unexpected(ring.error());
  }
  Path2f outline = path_from_ring(*ring);
  (void)paper_cut_content_key(bounds, params, flatten.tolerance);

  auto cached = cache.get_or_compile_fill(outline, FillRule::nonzero, flatten,
                                          tessellate);
  if (!cached) {
    return std::unexpected(cached.error());
  }

  // Flatten is cheap relative to tessellation and stays local for goldens /
  // mask hooks; the triangle mesh comes from the shared cache.
  auto flat = flatten_path(outline, flatten);
  if (!flat) {
    return std::unexpected(flat.error());
  }

  auto out =
      make_geometry_shell(bounds, params, std::move(outline), std::move(*flat));
  out.fill_mesh = **cached;
  return out;
}

std::string format_paper_cut_geometry_golden(std::string_view label,
                                             const PaperCutGeometry &geometry) {
  std::ostringstream os;
  os << "paper_cut " << label << '\n';
  os << std::fixed << std::setprecision(4);
  os << "seed=" << geometry.seed << " amp=" << geometry.amplitude_px
     << " wave=" << geometry.wavelength_px
     << " inset=" << geometry.safe_inset_px << '\n';
  os << "safe=" << geometry.safe_text_bounds.min_x << ','
     << geometry.safe_text_bounds.min_y << ','
     << geometry.safe_text_bounds.max_x << ','
     << geometry.safe_text_bounds.max_y << '\n';

  const std::size_t contour_pts =
      geometry.flattened.contours.empty()
          ? 0
          : geometry.flattened.contours.front().points.size();
  os << "outline_pts=" << contour_pts
     << " verts=" << geometry.fill_mesh.vertices.size()
     << " tris=" << geometry.fill_mesh.indices.size() / 3 << " checksum=0x"
     << std::hex << mesh_checksum(geometry.fill_mesh) << std::dec << '\n';

  if (!geometry.flattened.contours.empty()) {
    const auto &pts = geometry.flattened.contours.front().points;
    // Sparse outline dump: first, mid, last — enough to catch edge drift.
    const std::size_t idx_mid = pts.size() / 2;
    const std::size_t idxs[] = {0, idx_mid, pts.size() - 1};
    for (std::size_t k = 0; k < 3; ++k) {
      const auto &p = pts[idxs[k]];
      os << "p" << idxs[k] << '=' << p.x << ',' << p.y << '\n';
    }
  }
  return os.str();
}

} // namespace instantee::geometry
