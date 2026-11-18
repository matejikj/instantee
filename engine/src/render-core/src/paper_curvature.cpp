#include <instantee/render_core/paper_curvature.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace instantee::render_core {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "paper_curvature", message};
}

[[nodiscard]] bool finite_vec(const core::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

struct EdgeKey {
  std::uint32_t a = 0;
  std::uint32_t b = 0;

  [[nodiscard]] friend bool operator==(EdgeKey, EdgeKey) noexcept = default;
};

struct EdgeKeyHash {
  [[nodiscard]] std::size_t operator()(EdgeKey key) const noexcept {
    return (static_cast<std::size_t>(key.a) << 32U) ^
           static_cast<std::size_t>(key.b);
  }
};

[[nodiscard]] EdgeKey make_edge(std::uint32_t i, std::uint32_t j) noexcept {
  return i < j ? EdgeKey{i, j} : EdgeKey{j, i};
}

} // namespace

PaperCurvatureTier paper_curvature_tier_lite_v1() noexcept {
  return PaperCurvatureTier{};
}

std::expected<void, diagnostics::Error>
validate_paper_curvature_tier(const PaperCurvatureTier &tier) {
  if (tier.id.empty()) {
    return std::unexpected(make_err("invalid_tier", "Tier id is empty"));
  }
  if (tier.max_grid_u == 0 || tier.max_grid_v == 0 ||
      tier.max_ops_per_vertex == 0 || tier.max_total_ops == 0) {
    return std::unexpected(
        make_err("invalid_tier", "Tier budgets must be non-zero"));
  }
  return {};
}

std::expected<void, diagnostics::Error>
validate_paper_curvature_params(const PaperCurvatureParams &params) {
  if (!std::isfinite(params.bend_angle_rad) ||
      !std::isfinite(params.wave_amp_px) ||
      !std::isfinite(params.wave_freq_u) ||
      !std::isfinite(params.wave_freq_v) || !std::isfinite(params.wave_phase)) {
    return std::unexpected(
        make_err("non_finite", "Curvature params must be finite"));
  }
  if (params.mode != PaperCurvatureMode::bend &&
      params.mode != PaperCurvatureMode::wave) {
    return std::unexpected(make_err("invalid_mode", "Unknown curvature mode"));
  }
  // Keep lite deformations gentle enough for silhouette integrity.
  if (std::fabs(params.bend_angle_rad) > 3.141592653589793) {
    return std::unexpected(
        make_err("bend_out_of_range",
                 "bend_angle_rad must be within [-π, π] for lite tier"));
  }
  if (std::fabs(params.wave_amp_px) > 64.0) {
    return std::unexpected(make_err(
        "wave_out_of_range", "wave_amp_px must be within ±64 for lite tier"));
  }
  return {};
}

std::uint32_t paper_curvature_ops_per_vertex(PaperCurvatureMode mode) noexcept {
  switch (mode) {
  case PaperCurvatureMode::bend:
    return 6; // normalize + sin/cos + 2 muls/adds
  case PaperCurvatureMode::wave:
    return 4; // phase + sin + scale + add
  }
  return 8;
}

std::expected<PaperCurvatureMesh, diagnostics::Error>
make_paper_curvature_grid(double width, double height, std::uint32_t grid_u,
                          std::uint32_t grid_v,
                          const PaperCurvatureTier &tier) {
  if (auto ok = validate_paper_curvature_tier(tier); !ok) {
    return std::unexpected(ok.error());
  }
  if (!(width > 0.0) || !(height > 0.0) || !std::isfinite(width) ||
      !std::isfinite(height)) {
    return std::unexpected(
        make_err("invalid_size", "Grid width/height must be finite > 0"));
  }
  if (grid_u == 0 || grid_v == 0 || grid_u > tier.max_grid_u ||
      grid_v > tier.max_grid_v) {
    return std::unexpected(
        make_err("tier_exceeded", "Grid resolution exceeds curvature tier"));
  }

  PaperCurvatureMesh mesh;
  mesh.id = "curvature-grid";
  mesh.width = width;
  mesh.height = height;
  mesh.grid_u = grid_u;
  mesh.grid_v = grid_v;
  const std::uint32_t nu = grid_u + 1U;
  const std::uint32_t nv = grid_v + 1U;
  mesh.bind_vertices.reserve(static_cast<std::size_t>(nu) * nv);
  for (std::uint32_t v = 0; v < nv; ++v) {
    const double ty = static_cast<double>(v) / static_cast<double>(grid_v);
    for (std::uint32_t u = 0; u < nu; ++u) {
      const double tx = static_cast<double>(u) / static_cast<double>(grid_u);
      mesh.bind_vertices.push_back(core::Vec3{tx * width, ty * height, 0});
    }
  }
  mesh.indices.reserve(static_cast<std::size_t>(grid_u) * grid_v * 6U);
  for (std::uint32_t v = 0; v < grid_v; ++v) {
    for (std::uint32_t u = 0; u < grid_u; ++u) {
      const std::uint32_t i0 = v * nu + u;
      const std::uint32_t i1 = i0 + 1U;
      const std::uint32_t i2 = i0 + nu;
      const std::uint32_t i3 = i2 + 1U;
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }
  return mesh;
}

std::expected<void, diagnostics::Error>
validate_paper_curvature_mesh(const PaperCurvatureMesh &mesh,
                              const PaperCurvatureTier &tier) {
  if (auto ok = validate_paper_curvature_tier(tier); !ok) {
    return ok;
  }
  if (mesh.id.empty()) {
    return std::unexpected(
        make_err("invalid_id", "Curvature mesh id is empty"));
  }
  if (mesh.grid_u == 0 || mesh.grid_v == 0 || mesh.grid_u > tier.max_grid_u ||
      mesh.grid_v > tier.max_grid_v) {
    return std::unexpected(
        make_err("tier_exceeded", "Mesh grid exceeds curvature tier"));
  }
  const std::size_t expect_verts =
      static_cast<std::size_t>(mesh.grid_u + 1U) * (mesh.grid_v + 1U);
  if (mesh.bind_vertices.size() != expect_verts || mesh.indices.size() < 3U ||
      mesh.indices.size() % 3U != 0U) {
    return std::unexpected(
        make_err("invalid_topology", "Curvature mesh topology mismatch"));
  }
  for (const auto &v : mesh.bind_vertices) {
    if (!finite_vec(v)) {
      return std::unexpected(
          make_err("non_finite", "Bind vertices must be finite"));
    }
  }
  for (std::uint32_t idx : mesh.indices) {
    if (static_cast<std::size_t>(idx) >= mesh.bind_vertices.size()) {
      return std::unexpected(
          make_err("bad_index", "Triangle index out of range"));
    }
  }
  return {};
}

std::expected<PaperCurvaturePose, diagnostics::Error>
evaluate_paper_curvature(const PaperCurvatureMesh &mesh,
                         const PaperCurvatureParams &params,
                         const PaperCurvatureTier &tier) {
  if (auto ok = validate_paper_curvature_mesh(mesh, tier); !ok) {
    return std::unexpected(ok.error());
  }
  if (auto ok = validate_paper_curvature_params(params); !ok) {
    return std::unexpected(ok.error());
  }

  const std::uint32_t ops_each = paper_curvature_ops_per_vertex(params.mode);
  if (ops_each > tier.max_ops_per_vertex) {
    return std::unexpected(make_err(
        "tier_exceeded", "Mode ops_per_vertex exceeds curvature tier"));
  }
  const auto vert_count = static_cast<std::uint64_t>(mesh.bind_vertices.size());
  if (ops_each > 0 &&
      vert_count > tier.max_total_ops / static_cast<std::uint64_t>(ops_each)) {
    return std::unexpected(
        make_err("tier_exceeded", "Total curvature ops exceed tier budget"));
  }
  const std::uint64_t ops_used =
      vert_count * static_cast<std::uint64_t>(ops_each);

  PaperCurvaturePose pose;
  pose.indices = mesh.indices;
  pose.ops_used = ops_used;
  pose.tier = tier;
  pose.vertices.resize(mesh.bind_vertices.size());

  const double mid_x = mesh.width * 0.5;
  constexpr double kPi = 3.141592653589793;
  constexpr double kTwoPi = 6.283185307179586;

  for (std::size_t i = 0; i < mesh.bind_vertices.size(); ++i) {
    const core::Vec3 &b = mesh.bind_vertices[i];
    core::Vec3 out = b;
    if (params.mode == PaperCurvatureMode::bend) {
      const double angle = params.bend_angle_rad;
      if (std::fabs(angle) > 1e-12) {
        // Map x across width to arc parameter; keep chord length ≈ width.
        const double t = (b.x / mesh.width) - 0.5; // [-0.5, 0.5]
        const double radius = mesh.width / angle;
        const double theta = t * angle;
        out.x = mid_x + radius * std::sin(theta);
        out.z = radius * (1.0 - std::cos(theta));
        // Toward camera-forward (−Z) for positive bend.
        out.z = -out.z;
        out.y = b.y;
      }
    } else {
      const double u = mesh.width > 0.0 ? b.x / mesh.width : 0.0;
      const double v = mesh.height > 0.0 ? b.y / mesh.height : 0.0;
      const double phase =
          kTwoPi *
          (params.wave_freq_u * u + params.wave_freq_v * v + params.wave_phase);
      // Positive amp displaces toward camera-forward (−Z).
      out.z = b.z - params.wave_amp_px * std::sin(phase);
      (void)kPi;
    }
    if (!finite_vec(out)) {
      return std::unexpected(
          make_err("non_finite", "Curvature produced non-finite vertex"));
    }
    pose.vertices[i] = out;
  }
  return pose;
}

std::expected<PaperCurvatureSilhouetteReport, diagnostics::Error>
inspect_paper_curvature_silhouette(const PaperCurvatureMesh &mesh,
                                   const PaperCurvaturePose &pose) {
  if (pose.vertices.size() != mesh.bind_vertices.size() ||
      pose.indices.size() != mesh.indices.size()) {
    return std::unexpected(make_err(
        "pose_mismatch", "Pose topology does not match curvature mesh"));
  }

  PaperCurvatureSilhouetteReport report;
  report.ops_used = pose.ops_used;
  report.ops_budget = pose.tier.max_total_ops;
  report.within_tier = pose.ops_used <= pose.tier.max_total_ops;

  // Shared edges must occupy identical endpoints (no cracks).
  struct EdgePose {
    core::Vec3 a{};
    core::Vec3 b{};
    bool set = false;
  };
  std::unordered_map<EdgeKey, EdgePose, EdgeKeyHash> edges;
  double min_len = std::numeric_limits<double>::infinity();

  auto consider = [&](std::uint32_t i, std::uint32_t j) {
    const auto &pa = pose.vertices[i];
    const auto &pb = pose.vertices[j];
    const double dx = pa.x - pb.x;
    const double dy = pa.y - pb.y;
    const double dz = pa.z - pb.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    min_len = std::min(min_len, len);
    const EdgeKey key = make_edge(i, j);
    auto &slot = edges[key];
    if (!slot.set) {
      slot.a = pa;
      slot.b = pb;
      slot.set = true;
      return;
    }
    // Same undirected edge from the adjacent triangle — endpoints must match
    // as a set (order may flip).
    const bool same = (core::nearly_equal(slot.a, pa, 1e-7, 1e-7) &&
                       core::nearly_equal(slot.b, pb, 1e-7, 1e-7)) ||
                      (core::nearly_equal(slot.a, pb, 1e-7, 1e-7) &&
                       core::nearly_equal(slot.b, pa, 1e-7, 1e-7));
    if (!same) {
      ++report.shared_edge_mismatches;
    }
  };

  for (std::size_t t = 0; t + 2 < pose.indices.size(); t += 3) {
    const auto i0 = pose.indices[t];
    const auto i1 = pose.indices[t + 1];
    const auto i2 = pose.indices[t + 2];
    consider(i0, i1);
    consider(i1, i2);
    consider(i2, i0);
  }

  report.min_edge_length = std::isfinite(min_len) ? min_len : 0.0;
  report.no_collapsed_edges = report.min_edge_length > 1e-6;
  report.watertight = report.shared_edge_mismatches == 0;
  return report;
}

std::expected<void, diagnostics::Error>
register_paper_curvature_feature(ShaderFeatureRegistry &registry) {
  ShaderFeatureDesc feature;
  feature.id.name = std::string(kPaperCurvatureFeatureName);
  feature.id.version = 1;
  feature.params = {
      ShaderParamDesc{
          .name = "bend_angle_rad",
          .default_value =
              ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                 .scalar_f32 = 0.0f}},
      ShaderParamDesc{
          .name = "wave_amp_px",
          .default_value =
              ShaderParamDefault{.kind = ShaderParamKind::scalar_f32,
                                 .scalar_f32 = 0.0f}},
  };
  // One documented vertex-deform contribution; inactive feature adds nothing.
  feature.pass_contributions = {
      PassContribution{.pass_key = "paper_curvature_vertex_deform",
                       .sort_key = 40},
  };
  feature.capabilities.require_apple7 = true;
  return registry.register_feature(std::move(feature));
}

std::string format_paper_curvature_golden(std::string_view label,
                                          const PaperCurvatureMesh &mesh,
                                          const PaperCurvaturePose &pose,
                                          const PaperCurvatureParams &params) {
  std::ostringstream os;
  os << "paper_curvature " << label << '\n';
  os << "mesh=" << mesh.id << " grid=" << mesh.grid_u << "x" << mesh.grid_v
     << " mode=" << (params.mode == PaperCurvatureMode::bend ? "bend" : "wave")
     << " ops=" << pose.ops_used << " tier=" << pose.tier.id << '\n';
  os << std::fixed << std::setprecision(6);
  // Sample a few representative vertices: corners + center.
  const std::uint32_t nu = mesh.grid_u + 1U;
  const std::uint32_t nv = mesh.grid_v + 1U;
  const std::uint32_t samples[] = {
      0U, nu - 1U, (nv - 1U) * nu, nv * nu - 1U, (nv / 2U) * nu + (nu / 2U),
  };
  for (std::uint32_t idx : samples) {
    if (idx >= pose.vertices.size()) {
      continue;
    }
    const auto &v = pose.vertices[idx];
    os << "v " << idx << " x=" << v.x << " y=" << v.y << " z=" << v.z << '\n';
  }
  return os.str();
}

} // namespace instantee::render_core
