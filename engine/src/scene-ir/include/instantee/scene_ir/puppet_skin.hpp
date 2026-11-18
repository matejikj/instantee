#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/scene_ir/puppet_fk.hpp>
#include <instantee/scene_ir/puppet_rig.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::scene_ir {

inline constexpr std::uint8_t kPuppetSkinMaxInfluences = 4;

struct PuppetSkinInfluenceRef {
  std::uint32_t joint_index = 0;
  float weight = 0;
};

// Bind-pose vertex with CPU-precomputed influence indices (paper_2d_v1).
struct PuppetSkinVertex {
  core::Vec3 bind_position{};
  std::array<PuppetSkinInfluenceRef, kPuppetSkinMaxInfluences> influences{};
  std::uint8_t influence_count = 0;
};

// One paper part / layer mesh in bind space. Indices are optional triangles.
struct PuppetSkinPart {
  std::string layer_id;
  std::vector<PuppetSkinVertex> vertices;
  std::vector<std::uint32_t> indices;
};

// Per-joint skin matrices: skin[j] = posed_world[j] * inverse(bind_world[j]).
// Suitable for a later cheap GPU vertex path; CPU deform uses the same data.
struct PuppetSkinMatrices {
  std::vector<core::Mat4> matrices;
};

// Build a rectangular paper part for a documented layer. Every vertex receives
// the layer's authored skin_bindings influences. Fails closed when the layer
// or binding is missing / unsupported.
[[nodiscard]] std::expected<PuppetSkinPart, diagnostics::Error>
make_puppet_skin_layer_quad(const PuppetRigDocument &document,
                            const PuppetFkSkeleton &bind_skeleton,
                            std::string_view layer_id, double half_w,
                            double half_h);

[[nodiscard]] std::expected<PuppetSkinMatrices, diagnostics::Error>
compute_puppet_skin_matrices(const PuppetFkSkeleton &bind_skeleton,
                             const PuppetFkSkeleton &posed_skeleton);

// Linear-blend skinning in bind space. Unsupported influence counts, bad
// weights, or non-finite results are explicit errors (no soft fallback).
[[nodiscard]] std::expected<std::vector<core::Vec3>, diagnostics::Error>
deform_puppet_skin_part(const PuppetSkinPart &part,
                        const PuppetSkinMatrices &skin);

[[nodiscard]] std::string
format_puppet_skin_golden(std::string_view label, const PuppetSkinPart &part,
                          const std::vector<core::Vec3> &deformed,
                          double abs_tol = 1e-6);

} // namespace instantee::scene_ir
