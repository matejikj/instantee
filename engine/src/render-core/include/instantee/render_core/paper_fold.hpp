#pragma once

#include <instantee/core_model/time.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::render_core {

// CPU paper fold/unfold without a physics simulator. A sheet is a bind-space
// triangle mesh split into segments that hinge about authored crease lines.
// Fold angles are keyframed in radians (0 = flat, +π = fully folded over the
// crease toward camera-forward −Z). Occlusion uses segment order plus posed Z.

struct PaperFoldCrease {
  std::string id;
  // Line in bind XY: point + direction (normalized on validate).
  core::Vec3 origin{};
  core::Vec3 direction{1, 0, 0};
};

struct PaperFoldSegment {
  std::string id;
  // Empty crease_id ⇒ fixed base panel (never rotated).
  std::string crease_id;
  std::vector<std::uint32_t> vertex_indices;
  // Higher draws above when fold depth is ambiguous (|angle| near π/2).
  std::int32_t occlusion_order = 0;
};

struct PaperFoldMesh {
  std::string id;
  std::vector<core::Vec3> bind_vertices;
  std::vector<std::uint32_t> indices;
  std::vector<PaperFoldCrease> creases;
  std::vector<PaperFoldSegment> segments;
};

struct PaperFoldKeyframe {
  core::FrameIndex frame{};
  // crease_id → hinge angle (radians). Omitted creases hold 0.
  std::unordered_map<std::string, double> crease_angles;
};

struct PaperFoldClip {
  std::string id;
  std::vector<PaperFoldKeyframe> keys;
};

struct PaperFoldSegmentPose {
  std::string segment_id;
  std::string crease_id;
  double angle_rad = 0;
  std::int32_t occlusion_order = 0;
  // Representative posed depth (mean vertex z) for sort ties.
  double depth_z = 0;
};

struct PaperFoldPose {
  std::vector<core::Vec3> vertices;
  std::vector<std::uint32_t> indices;
  // Segments sorted back-to-front for transparent paper draws.
  std::vector<PaperFoldSegmentPose> draw_order;
};

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_fold_mesh(const PaperFoldMesh &mesh);

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_paper_fold_clip(const PaperFoldClip &clip, const PaperFoldMesh &mesh);

// Two-panel letter card: fixed left half, right half hinges about x=width/2.
[[nodiscard]] PaperFoldMesh make_letter_fold_mesh(double width = 120.0,
                                                  double height = 80.0);

// Flat → folded → flat clip on the single "hinge" crease.
[[nodiscard]] PaperFoldClip
make_letter_fold_clip(std::uint64_t fold_frames = 24,
                      std::uint64_t hold_frames = 6);

// Sample crease angles at an absolute frame (linear between keys, clamp ends).
[[nodiscard]] std::expected<std::unordered_map<std::string, double>,
                            diagnostics::Error>
sample_paper_fold_angles(const PaperFoldClip &clip, const PaperFoldMesh &mesh,
                         core::FrameIndex frame);

[[nodiscard]] std::expected<PaperFoldPose, diagnostics::Error>
evaluate_paper_fold(
    const PaperFoldMesh &mesh,
    const std::unordered_map<std::string, double> &crease_angles);

[[nodiscard]] std::expected<PaperFoldPose, diagnostics::Error>
evaluate_paper_fold_clip(const PaperFoldMesh &mesh, const PaperFoldClip &clip,
                         core::FrameIndex frame);

[[nodiscard]] std::string format_paper_fold_golden(std::string_view label,
                                                   const PaperFoldMesh &mesh,
                                                   const PaperFoldPose &pose,
                                                   core::FrameIndex frame);

} // namespace instantee::render_core
