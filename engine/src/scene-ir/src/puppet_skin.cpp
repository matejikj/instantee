#include <instantee/scene_ir/puppet_skin.hpp>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace instantee::scene_ir {
namespace {

diagnostics::Error make_err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "puppet_skin", message};
}

[[nodiscard]] bool finite_vec(const core::Vec3 &v) noexcept {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] std::expected<core::Mat4, diagnostics::Error>
invert_affine(const core::Mat4 &m) {
  if (!core::nearly_equal(m(3, 0), 0.0) || !core::nearly_equal(m(3, 1), 0.0) ||
      !core::nearly_equal(m(3, 2), 0.0) || !core::nearly_equal(m(3, 3), 1.0)) {
    return std::unexpected(
        make_err("non_affine", "Skin bind matrix must be affine"));
  }

  const double a00 = m(0, 0);
  const double a01 = m(0, 1);
  const double a02 = m(0, 2);
  const double a10 = m(1, 0);
  const double a11 = m(1, 1);
  const double a12 = m(1, 2);
  const double a20 = m(2, 0);
  const double a21 = m(2, 1);
  const double a22 = m(2, 2);

  const double c00 = a11 * a22 - a12 * a21;
  const double c01 = a02 * a21 - a01 * a22;
  const double c02 = a01 * a12 - a02 * a11;
  const double c10 = a12 * a20 - a10 * a22;
  const double c11 = a00 * a22 - a02 * a20;
  const double c12 = a02 * a10 - a00 * a12;
  const double c20 = a10 * a21 - a11 * a20;
  const double c21 = a01 * a20 - a00 * a21;
  const double c22 = a00 * a11 - a01 * a10;
  const double det = a00 * c00 + a01 * c10 + a02 * c20;
  if (!std::isfinite(det) || std::fabs(det) < 1e-12) {
    return std::unexpected(
        make_err("singular_matrix", "Skin bind matrix is singular"));
  }
  const double inv_det = 1.0 / det;

  core::Mat4 out = core::Mat4::identity();
  out(0, 0) = c00 * inv_det;
  out(0, 1) = c01 * inv_det;
  out(0, 2) = c02 * inv_det;
  out(1, 0) = c10 * inv_det;
  out(1, 1) = c11 * inv_det;
  out(1, 2) = c12 * inv_det;
  out(2, 0) = c20 * inv_det;
  out(2, 1) = c21 * inv_det;
  out(2, 2) = c22 * inv_det;

  const double tx = m(0, 3);
  const double ty = m(1, 3);
  const double tz = m(2, 3);
  out(0, 3) = -(out(0, 0) * tx + out(0, 1) * ty + out(0, 2) * tz);
  out(1, 3) = -(out(1, 0) * tx + out(1, 1) * ty + out(1, 2) * tz);
  out(2, 3) = -(out(2, 0) * tx + out(2, 1) * ty + out(2, 2) * tz);
  return out;
}

[[nodiscard]] std::expected<std::vector<PuppetSkinInfluenceRef>,
                            diagnostics::Error>
resolve_layer_influences(const PuppetRigDocument &document,
                         const PuppetFkSkeleton &bind_skeleton,
                         std::string_view layer_id) {
  if (document.retarget.profile != kPuppetRigRetargetProfile ||
      document.retarget.space != kPuppetRigRetargetSpace) {
    return std::unexpected(make_err(
        "unsupported_rig",
        "Skinning supports only paper_2d_v1 / 2d; no natural fallback"));
  }

  const PuppetSkinBinding *binding = nullptr;
  for (const auto &entry : document.skin_bindings) {
    if (entry.layer_id == layer_id) {
      binding = &entry;
      break;
    }
  }
  if (binding == nullptr) {
    return std::unexpected(make_err(
        "missing_skin_binding",
        "Layer has no skin_bindings entry; unsupported without explicit bind"));
  }
  if (binding->influences.empty() ||
      binding->influences.size() > kPuppetSkinMaxInfluences ||
      binding->influences.size() >
          static_cast<std::size_t>(
              document.retarget.max_influences_per_vertex)) {
    return std::unexpected(
        make_err("unsupported_influences",
                 "Influence count outside paper_2d_v1 budget; explicit error"));
  }

  std::vector<PuppetSkinInfluenceRef> out;
  out.reserve(binding->influences.size());
  double sum = 0.0;
  for (const auto &inf : binding->influences) {
    const auto it = bind_skeleton.index_by_id.find(inf.joint_id);
    if (it == bind_skeleton.index_by_id.end()) {
      return std::unexpected(
          make_err("unknown_joint", "Skin influence joint is unknown"));
    }
    if (!(inf.weight > 0.0) || inf.weight > 1.0 || !std::isfinite(inf.weight)) {
      return std::unexpected(
          make_err("invalid_weight", "Skin weight out of range"));
    }
    sum += inf.weight;
    out.push_back(PuppetSkinInfluenceRef{
        .joint_index = static_cast<std::uint32_t>(it->second),
        .weight = static_cast<float>(inf.weight),
    });
  }
  if (!std::isfinite(sum) || std::abs(sum - 1.0) > 1e-6) {
    return std::unexpected(
        make_err("invalid_skin_weights", "Skin weights must sum to 1"));
  }
  return out;
}

} // namespace

std::expected<PuppetSkinPart, diagnostics::Error> make_puppet_skin_layer_quad(
    const PuppetRigDocument &document, const PuppetFkSkeleton &bind_skeleton,
    std::string_view layer_id, double half_w, double half_h) {
  if (!(half_w > 0.0) || !(half_h > 0.0) || !std::isfinite(half_w) ||
      !std::isfinite(half_h)) {
    return std::unexpected(make_err(
        "invalid_extents", "Quad half extents must be finite and > 0"));
  }

  const PuppetLayer *layer = nullptr;
  for (const auto &entry : document.layers) {
    if (entry.id == layer_id) {
      layer = &entry;
      break;
    }
  }
  if (layer == nullptr) {
    return std::unexpected(make_err("unknown_layer", "Layer id is unknown"));
  }
  const auto bind_it = bind_skeleton.index_by_id.find(layer->bind_joint_id);
  if (bind_it == bind_skeleton.index_by_id.end()) {
    return std::unexpected(
        make_err("unknown_joint", "Layer bind_joint_id is unknown"));
  }

  auto influences = resolve_layer_influences(document, bind_skeleton, layer_id);
  if (!influences) {
    return std::unexpected(influences.error());
  }

  const core::Vec3 center =
      puppet_fk_world_position(bind_skeleton.joints[bind_it->second]);
  if (!finite_vec(center)) {
    return std::unexpected(
        make_err("non_finite", "Bind joint position must be finite"));
  }

  PuppetSkinPart part;
  part.layer_id = std::string(layer_id);
  part.vertices.resize(4);
  const std::array<core::Vec3, 4> corners = {
      core::Vec3{center.x - half_w, center.y - half_h, 0.0},
      core::Vec3{center.x + half_w, center.y - half_h, 0.0},
      core::Vec3{center.x + half_w, center.y + half_h, 0.0},
      core::Vec3{center.x - half_w, center.y + half_h, 0.0},
  };
  for (std::size_t i = 0; i < corners.size(); ++i) {
    auto &vertex = part.vertices[i];
    vertex.bind_position = corners[i];
    vertex.influence_count = static_cast<std::uint8_t>(influences->size());
    for (std::size_t j = 0; j < influences->size(); ++j) {
      vertex.influences[j] = (*influences)[j];
    }
  }
  part.indices = {0, 1, 2, 0, 2, 3};
  return part;
}

std::expected<PuppetSkinMatrices, diagnostics::Error>
compute_puppet_skin_matrices(const PuppetFkSkeleton &bind_skeleton,
                             const PuppetFkSkeleton &posed_skeleton) {
  if (bind_skeleton.rig_id != posed_skeleton.rig_id ||
      bind_skeleton.joints.size() != posed_skeleton.joints.size()) {
    return std::unexpected(
        make_err("rig_mismatch", "Bind and posed skeletons do not match"));
  }

  PuppetSkinMatrices out;
  out.matrices.resize(bind_skeleton.joints.size(), core::Mat4::identity());
  for (std::size_t i = 0; i < bind_skeleton.joints.size(); ++i) {
    if (bind_skeleton.joints[i].id != posed_skeleton.joints[i].id) {
      return std::unexpected(
          make_err("rig_mismatch", "Joint order differs between skeletons"));
    }
    auto inv = invert_affine(bind_skeleton.joints[i].world_matrix);
    if (!inv) {
      return std::unexpected(inv.error());
    }
    out.matrices[i] = core::mul(posed_skeleton.joints[i].world_matrix, *inv);
  }
  return out;
}

std::expected<std::vector<core::Vec3>, diagnostics::Error>
deform_puppet_skin_part(const PuppetSkinPart &part,
                        const PuppetSkinMatrices &skin) {
  std::vector<core::Vec3> deformed;
  deformed.reserve(part.vertices.size());
  for (const auto &vertex : part.vertices) {
    if (vertex.influence_count == 0U ||
        vertex.influence_count > kPuppetSkinMaxInfluences) {
      return std::unexpected(make_err(
          "unsupported_influences",
          "Vertex influence count outside paper_2d_v1; explicit error"));
    }
    if (!finite_vec(vertex.bind_position)) {
      return std::unexpected(
          make_err("non_finite", "Bind position must be finite"));
    }

    double sum_w = 0.0;
    core::Vec3 accum{};
    for (std::uint8_t i = 0; i < vertex.influence_count; ++i) {
      const auto &inf = vertex.influences[i];
      if (inf.joint_index >= skin.matrices.size()) {
        return std::unexpected(
            make_err("dangling_ref", "Skin joint_index out of range"));
      }
      if (!(inf.weight > 0.0f) || inf.weight > 1.0f ||
          !std::isfinite(inf.weight)) {
        return std::unexpected(
            make_err("invalid_weight", "Skin weight out of range"));
      }
      sum_w += static_cast<double>(inf.weight);
      const core::Vec3 p = core::transform_point(skin.matrices[inf.joint_index],
                                                 vertex.bind_position);
      if (!finite_vec(p)) {
        return std::unexpected(
            make_err("non_finite", "Skinned position became non-finite"));
      }
      accum.x += p.x * static_cast<double>(inf.weight);
      accum.y += p.y * static_cast<double>(inf.weight);
      accum.z += p.z * static_cast<double>(inf.weight);
    }
    if (!std::isfinite(sum_w) || std::abs(sum_w - 1.0) > 1e-5) {
      return std::unexpected(
          make_err("invalid_skin_weights", "Vertex weights must sum to 1"));
    }
    if (!finite_vec(accum)) {
      return std::unexpected(
          make_err("non_finite", "Skinned blend became non-finite"));
    }
    deformed.push_back(accum);
  }
  return deformed;
}

std::string format_puppet_skin_golden(std::string_view label,
                                      const PuppetSkinPart &part,
                                      const std::vector<core::Vec3> &deformed,
                                      double abs_tol) {
  std::ostringstream os;
  os << "puppet_skin " << label << '\n';
  os << "layer=" << part.layer_id << " verts=" << deformed.size()
     << " tris=" << (part.indices.size() / 3U) << " tol=" << std::fixed
     << std::setprecision(6) << abs_tol << '\n';
  for (std::size_t i = 0; i < deformed.size(); ++i) {
    const auto &b = part.vertices[i].bind_position;
    const auto &d = deformed[i];
    os << "v" << i << " bind=" << std::setprecision(6) << b.x << ',' << b.y
       << " def=" << d.x << ',' << d.y << '\n';
  }
  return os.str();
}

} // namespace instantee::scene_ir
