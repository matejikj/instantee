#pragma once

#include <instantee/core_model/camera.hpp>
#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace instantee::core {

// Layered 2.5D paper stage (SAS §8.1 / FR-05). Planes are flat cards in
// world space at positive depthUnits in front of the camera (world z = -depth).
// Stage anchors and layer clips stay separate from LocalTransform pivot.

inline constexpr std::size_t kDepthStageMinLayers = 10;
inline constexpr std::size_t kDepthStageMaxLayers = 20;

namespace detail {

inline diagnostics::Error stage_err(std::string_view code,
                                    std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "depth_stage", message};
}

} // namespace detail

// Layout pin on the stage canvas. Not a transform pivot (SAS §8.1).
struct StageAnchor {
  double x = 0;
  double y = 0;
};

// Axis-aligned clip in a paper plane's local XY (canvas pixels). Distinct from
// geometry bounds and from StageAnchor.
struct LayerClip {
  double min_x = 0;
  double min_y = 0;
  double max_x = 0;
  double max_y = 0;
};

// One spatial paper plane. depth is a positive distance in front of the camera
// (depthUnits). layer_order is the explicit transparent sort key; world z is
// derived from depth for perspective/parallax only.
struct PaperPlane {
  std::uint32_t layer_order = 0;
  double depth = 1;
  double width = 1;
  double height = 1;
  StageAnchor anchor{};
  LayerClip clip{};
};

struct DepthStage {
  StageAnchor origin{};
  double focus_depth = 10;
  std::vector<PaperPlane> planes{};
};

struct DepthStageSpacing {
  // Positive depth of the nearest plane (depthUnits).
  double near_depth = 4;
  // Positive step added per subsequent plane (farther from camera).
  double depth_step = 1.5;
  double plane_width = 1080;
  double plane_height = 1920;
  StageAnchor origin{};
  // Focus plane used by parallax_factor; defaults to the mid-depth plane.
  double focus_depth = 0;
};

[[nodiscard]] constexpr bool is_finite_stage_anchor(StageAnchor a) noexcept {
  return std::isfinite(a.x) && std::isfinite(a.y);
}

[[nodiscard]] constexpr bool
is_finite_layer_clip(const LayerClip &clip) noexcept {
  return std::isfinite(clip.min_x) && std::isfinite(clip.min_y) &&
         std::isfinite(clip.max_x) && std::isfinite(clip.max_y);
}

[[nodiscard]] constexpr bool
is_normalized_layer_clip(const LayerClip &clip) noexcept {
  return clip.min_x <= clip.max_x && clip.min_y <= clip.max_y;
}

[[nodiscard]] constexpr bool layer_clip_contains(const LayerClip &clip,
                                                 double x, double y) noexcept {
  return x >= clip.min_x && x <= clip.max_x && y >= clip.min_y &&
         y <= clip.max_y;
}

// World matrix placing the plane so its StageAnchor coincides with
// stage.origin in XY and world z = -depth (camera forward = -Z). Local point
// (anchor.x, anchor.y, 0) maps to (origin.x, origin.y, -depth).
[[nodiscard]] inline Mat4 plane_world_matrix(const DepthStage &stage,
                                             const PaperPlane &plane) noexcept {
  LocalTransform local{};
  local.translate = {stage.origin.x - plane.anchor.x,
                     stage.origin.y - plane.anchor.y, -plane.depth};
  return local_matrix(local);
}

[[nodiscard]] inline Vec3 plane_anchor_world(const DepthStage &stage,
                                             const PaperPlane &plane) noexcept {
  return transform_point(plane_world_matrix(stage, plane),
                         Vec3{plane.anchor.x, plane.anchor.y, 0.0});
}

[[nodiscard]] inline std::expected<void, diagnostics::Error>
validate_paper_plane(const PaperPlane &plane) {
  if (!(plane.depth > 0.0) || !std::isfinite(plane.depth)) {
    return std::unexpected(detail::stage_err(
        "invalid_depth", "Paper plane depth must be finite and positive"));
  }
  if (!(plane.width > 0.0) || !(plane.height > 0.0) ||
      !std::isfinite(plane.width) || !std::isfinite(plane.height)) {
    return std::unexpected(detail::stage_err(
        "invalid_size", "Paper plane size must be finite and positive"));
  }
  if (!is_finite_stage_anchor(plane.anchor)) {
    return std::unexpected(detail::stage_err(
        "invalid_anchor", "Paper plane anchor must be finite"));
  }
  if (!is_finite_layer_clip(plane.clip) ||
      !is_normalized_layer_clip(plane.clip)) {
    return std::unexpected(detail::stage_err(
        "invalid_clip", "Layer clip must be finite and normalized"));
  }
  return {};
}

[[nodiscard]] inline std::expected<void, diagnostics::Error>
validate_depth_stage(const DepthStage &stage) {
  if (!is_finite_stage_anchor(stage.origin)) {
    return std::unexpected(detail::stage_err(
        "invalid_origin", "Stage origin anchor must be finite"));
  }
  if (!(stage.focus_depth > 0.0) || !std::isfinite(stage.focus_depth)) {
    return std::unexpected(detail::stage_err(
        "invalid_focus", "Stage focus depth must be finite and positive"));
  }
  const std::size_t count = stage.planes.size();
  if (count < kDepthStageMinLayers || count > kDepthStageMaxLayers) {
    return std::unexpected(detail::stage_err(
        "invalid_layer_count",
        "Depth stage requires between 10 and 20 paper planes"));
  }
  double previous_depth = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const PaperPlane &plane = stage.planes[i];
    if (auto ok = validate_paper_plane(plane); !ok) {
      return ok;
    }
    if (!(plane.depth > previous_depth)) {
      return std::unexpected(detail::stage_err(
          "non_monotonic_depth",
          "Paper plane depths must be strictly increasing (near to far)"));
    }
    previous_depth = plane.depth;
  }
  return {};
}

// Build a regularly spaced stack of paper planes. Layer 0 is nearest.
[[nodiscard]] inline std::expected<DepthStage, diagnostics::Error>
make_layered_depth_stage(std::size_t layer_count,
                         const DepthStageSpacing &spacing = {}) {
  if (layer_count < kDepthStageMinLayers ||
      layer_count > kDepthStageMaxLayers) {
    return std::unexpected(detail::stage_err(
        "invalid_layer_count",
        "Depth stage requires between 10 and 20 paper planes"));
  }
  if (!(spacing.near_depth > 0.0) || !std::isfinite(spacing.near_depth) ||
      !(spacing.depth_step > 0.0) || !std::isfinite(spacing.depth_step) ||
      !(spacing.plane_width > 0.0) || !std::isfinite(spacing.plane_width) ||
      !(spacing.plane_height > 0.0) || !std::isfinite(spacing.plane_height) ||
      !is_finite_stage_anchor(spacing.origin)) {
    return std::unexpected(detail::stage_err(
        "invalid_spacing",
        "Depth stage spacing fields must be finite and positive"));
  }

  const double last_depth =
      spacing.near_depth +
      static_cast<double>(layer_count - 1U) * spacing.depth_step;
  if (!std::isfinite(last_depth)) {
    return std::unexpected(detail::stage_err(
        "depth_overflow", "Layer depths overflowed finite range"));
  }

  double focus = spacing.focus_depth;
  if (!(focus > 0.0) || !std::isfinite(focus)) {
    focus = spacing.near_depth +
            static_cast<double>(layer_count / 2U) * spacing.depth_step;
  }

  DepthStage stage{};
  stage.origin = spacing.origin;
  stage.focus_depth = focus;
  stage.planes.reserve(layer_count);
  for (std::size_t i = 0; i < layer_count; ++i) {
    PaperPlane plane{};
    plane.layer_order = static_cast<std::uint32_t>(i);
    plane.depth =
        spacing.near_depth + static_cast<double>(i) * spacing.depth_step;
    plane.width = spacing.plane_width;
    plane.height = spacing.plane_height;
    // Pin the geometric center on the stage origin by default.
    plane.anchor = {0.5 * plane.width, 0.5 * plane.height};
    plane.clip = {0.0, 0.0, plane.width, plane.height};
    stage.planes.push_back(plane);
  }

  if (auto ok = validate_depth_stage(stage); !ok) {
    return std::unexpected(ok.error());
  }
  return stage;
}

struct PlaneScreenSample {
  std::uint32_t layer_order = 0;
  double depth = 0;
  double parallax_factor = 0;
  Vec3 world{};
  Vec3 pixel{};
  DepthClip depth_clip = DepthClip::inside;
};

// Project one plane anchor. near/far are positive camera-forward distances used
// only for DepthClip classification (same convention as PerspectiveFrustum).
[[nodiscard]] inline std::expected<PlaneScreenSample, diagnostics::Error>
sample_plane_screen(const DepthStage &stage, const PaperPlane &plane,
                    const CameraMatrices &camera, double near_distance = 0.1,
                    double far_distance = 1000.0) {
  if (auto ok = validate_paper_plane(plane); !ok) {
    return std::unexpected(ok.error());
  }
  auto factor = parallax_factor(plane.depth, stage.focus_depth);
  if (!factor) {
    return std::unexpected(factor.error());
  }
  const Vec3 world = plane_anchor_world(stage, plane);
  auto pixel = project_world_to_pixel(camera, world);
  if (!pixel) {
    return std::unexpected(pixel.error());
  }
  const Vec3 view = transform_point(camera.view, world);
  PlaneScreenSample sample{};
  sample.layer_order = plane.layer_order;
  sample.depth = plane.depth;
  sample.parallax_factor = *factor;
  sample.world = world;
  sample.pixel = *pixel;
  sample.depth_clip = classify_view_depth(view.z, near_distance, far_distance);
  return sample;
}

[[nodiscard]] inline std::expected<std::vector<PlaneScreenSample>,
                                   diagnostics::Error>
sample_depth_stage_screen(const DepthStage &stage, const CameraMatrices &camera,
                          double near_distance = 0.1,
                          double far_distance = 1000.0) {
  if (auto ok = validate_depth_stage(stage); !ok) {
    return std::unexpected(ok.error());
  }
  std::vector<PlaneScreenSample> samples;
  samples.reserve(stage.planes.size());
  for (const PaperPlane &plane : stage.planes) {
    auto sample =
        sample_plane_screen(stage, plane, camera, near_distance, far_distance);
    if (!sample) {
      return std::unexpected(sample.error());
    }
    samples.push_back(*sample);
  }
  return samples;
}

struct PlaneParallaxDelta {
  std::uint32_t layer_order = 0;
  double depth = 0;
  double parallax_factor = 0;
  double dx_px = 0;
  double dy_px = 0;
};

// Screen-space motion of each plane anchor between two camera poses. Under a
// pure camera pan, |dx| scales with parallax_factor (nearer layers move more).
[[nodiscard]] inline std::expected<std::vector<PlaneParallaxDelta>,
                                   diagnostics::Error>
measure_stage_parallax(const DepthStage &stage, const CameraMatrices &before,
                       const CameraMatrices &after) {
  auto a = sample_depth_stage_screen(stage, before);
  if (!a) {
    return std::unexpected(a.error());
  }
  auto b = sample_depth_stage_screen(stage, after);
  if (!b) {
    return std::unexpected(b.error());
  }
  std::vector<PlaneParallaxDelta> deltas;
  deltas.reserve(a->size());
  for (std::size_t i = 0; i < a->size(); ++i) {
    PlaneParallaxDelta delta{};
    delta.layer_order = (*a)[i].layer_order;
    delta.depth = (*a)[i].depth;
    delta.parallax_factor = (*a)[i].parallax_factor;
    delta.dx_px = (*b)[i].pixel.x - (*a)[i].pixel.x;
    delta.dy_px = (*b)[i].pixel.y - (*a)[i].pixel.y;
    deltas.push_back(delta);
  }
  return deltas;
}

} // namespace instantee::core
