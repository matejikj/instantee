#include <instantee/core_model/depth_stage.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

} // namespace

int main() {
  using namespace instantee::core;

  constexpr double kWidth = 1080;
  constexpr double kHeight = 1920;
  Viewport viewport{0, 0, kWidth, kHeight};

  PerspectiveFrustum frustum;
  frustum.fov_y_rad = std::numbers::pi_v<double> / 3.0;
  frustum.aspect = kWidth / kHeight;
  frustum.near_distance = 1.0;
  frustum.far_distance = 100.0;

  DepthStageSpacing spacing{};
  spacing.near_depth = 5.0;
  spacing.depth_step = 1.5;
  spacing.plane_width = kWidth;
  spacing.plane_height = kHeight;
  spacing.origin = {kWidth * 0.5, kHeight * 0.5};
  spacing.focus_depth = 0; // mid-stack default

  {
    expect(!make_layered_depth_stage(9, spacing).has_value(),
           "reject fewer than 10 layers");
    expect(!make_layered_depth_stage(21, spacing).has_value(),
           "reject more than 20 layers");
    DepthStageSpacing bad = spacing;
    bad.depth_step = 0;
    expect(!make_layered_depth_stage(12, bad).has_value(),
           "reject non-positive depth step");
  }

  auto stage = make_layered_depth_stage(16, spacing);
  expect(stage.has_value(), "build 16-layer stage");
  if (!stage) {
    return EXIT_FAILURE;
  }
  expect(stage->planes.size() == 16, "sixteen planes");
  expect(validate_depth_stage(*stage).has_value(), "stage validates");

  {
    // Stage anchors pin plane centers on the stage origin in XY.
    const PaperPlane &near = stage->planes.front();
    const Vec3 world = plane_anchor_world(*stage, near);
    expect(nearly_equal(world.x, spacing.origin.x), "anchor world x");
    expect(nearly_equal(world.y, spacing.origin.y), "anchor world y");
    expect(nearly_equal(world.z, -near.depth), "anchor world z = -depth");
  }

  {
    // Layer clips cover the full plane by default; content outside fails.
    const PaperPlane &plane = stage->planes[0];
    expect(
        layer_clip_contains(plane.clip, plane.width * 0.5, plane.height * 0.5),
        "center inside clip");
    expect(!layer_clip_contains(plane.clip, -1.0, 0.0), "outside clip left");
    expect(!layer_clip_contains(plane.clip, plane.width + 1.0, 0.0),
           "outside clip right");

    PaperPlane clipped = plane;
    clipped.clip = {100, 200, 300, 400};
    expect(validate_paper_plane(clipped).has_value(), "custom clip ok");
    expect(layer_clip_contains(clipped.clip, 150, 250), "custom inside");
    expect(!layer_clip_contains(clipped.clip, 50, 250), "custom outside");
    clipped.clip = {10, 10, 5, 20};
    expect(!validate_paper_plane(clipped).has_value(), "reject denormal clip");
  }

  {
    // Non-center stage anchor shifts the plane body, not the pinned world
    // anchor point relative to stage.origin.
    PaperPlane shifted = stage->planes[0];
    shifted.anchor = {0.0, 0.0};
    const Vec3 world = plane_anchor_world(*stage, shifted);
    expect(nearly_equal(world.x, spacing.origin.x), "corner anchor world x");
    expect(nearly_equal(world.y, spacing.origin.y), "corner anchor world y");
    const Mat4 world_m = plane_world_matrix(*stage, shifted);
    const Vec3 center = transform_point(
        world_m, Vec3{shifted.width * 0.5, shifted.height * 0.5, 0});
    expect(center.x > world.x, "plane body extends +X from corner anchor");
    expect(center.y > world.y, "plane body extends +Y from corner anchor");
  }

  auto rest = make_perspective_camera(Mat4::identity(), frustum, viewport,
                                      AspectFit::stretch, kWidth, kHeight);
  expect(rest.has_value(), "rest camera");
  if (!rest) {
    return EXIT_FAILURE;
  }

  LocalTransform pan{};
  pan.translate = {3.0, 0.0, 0.0};
  auto trucked = make_perspective_camera(local_matrix(pan), frustum, viewport,
                                         AspectFit::stretch, kWidth, kHeight);
  expect(trucked.has_value(), "trucked camera");
  if (!trucked) {
    return EXIT_FAILURE;
  }

  {
    auto samples = sample_depth_stage_screen(
        *stage, *rest, frustum.near_distance, frustum.far_distance);
    expect(samples.has_value(), "rest samples");
    if (samples) {
      expect(samples->size() == 16, "sample count");
      for (const PlaneScreenSample &s : *samples) {
        expect(s.depth_clip == DepthClip::inside, "plane inside frustum");
        expect(s.parallax_factor > 0.0, "positive parallax factor");
      }
      // Axes: +X still increases pixel x for a rightward world offset.
      PaperPlane probe = stage->planes[0];
      const Vec3 right_world =
          transform_point(plane_world_matrix(*stage, probe),
                          Vec3{probe.anchor.x + 40.0, probe.anchor.y, 0.0});
      auto origin_px = project_world_to_pixel(*rest, samples->front().world);
      auto right_px = project_world_to_pixel(*rest, right_world);
      expect(origin_px && right_px, "axis projections");
      if (origin_px && right_px) {
        expect(right_px->x > origin_px->x, "+X increases pixel x");
      }
    }
  }

  {
    auto deltas = measure_stage_parallax(*stage, *rest, *trucked);
    expect(deltas.has_value(), "parallax deltas");
    if (!deltas) {
      return EXIT_FAILURE;
    }
    expect(deltas->size() == 16, "delta count");

    // Nearer layers move more; motion is strictly decreasing with depth.
    for (std::size_t i = 0; i + 1 < deltas->size(); ++i) {
      const double near_dx = std::fabs((*deltas)[i].dx_px);
      const double far_dx = std::fabs((*deltas)[i + 1].dx_px);
      expect(near_dx > far_dx, "nearer layer has larger |dx|");
      expect((*deltas)[i].parallax_factor > (*deltas)[i + 1].parallax_factor,
             "parallax_factor decreases with depth");
    }
    expect(std::fabs(deltas->front().dx_px) > 0.0, "nearest layer moves");
    expect(std::fabs(deltas->back().dx_px) > 0.0, "farthest layer moves");

    // Screen motion scales with parallax_factor under a pure lateral truck.
    const double ref_factor = deltas->front().parallax_factor;
    const double ref_dx = std::fabs(deltas->front().dx_px);
    expect(ref_factor > 0.0 && ref_dx > 0.0, "reference motion");
    for (const PlaneParallaxDelta &d : *deltas) {
      const double predicted = ref_dx * (d.parallax_factor / ref_factor);
      expect(nearly_equal(std::fabs(d.dx_px), predicted, 1e-6, 1e-6),
             "dx scales with parallax_factor");
      expect(nearly_equal(d.dy_px, 0.0, 1e-6, 1e-6), "pure X truck: dy ~ 0");
    }
  }

  {
    // 10 and 20 layer bounds remain valid stage scenes.
    auto ten = make_layered_depth_stage(10, spacing);
    auto twenty = make_layered_depth_stage(20, spacing);
    expect(ten && twenty, "boundary layer counts");
    if (ten && twenty) {
      auto d10 = measure_stage_parallax(*ten, *rest, *trucked);
      auto d20 = measure_stage_parallax(*twenty, *rest, *trucked);
      expect(d10 && d20, "boundary parallax");
      if (d10 && d20) {
        expect(std::fabs(d10->front().dx_px) > std::fabs(d10->back().dx_px),
               "10-layer near>far");
        expect(std::fabs(d20->front().dx_px) > std::fabs(d20->back().dx_px),
               "20-layer near>far");
      }
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
