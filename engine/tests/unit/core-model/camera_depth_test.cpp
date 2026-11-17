#include <instantee/core_model/camera.hpp>

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
  frustum.fov_y_rad = std::numbers::pi_v<double> / 3.0; // 60°
  frustum.aspect = kWidth / kHeight;
  frustum.near_distance = 1.0;
  frustum.far_distance = 100.0;

  {
    auto camera = make_perspective_camera(Mat4::identity(), frustum, viewport,
                                          AspectFit::stretch, kWidth, kHeight);
    expect(camera.has_value(), "perspective camera");
    if (!camera) {
      return EXIT_FAILURE;
    }

    // Axes: +X and +Y stay uninverted in pixel space.
    auto origin = project_world_to_pixel(*camera, Vec3{0, 0, -10});
    auto right = project_world_to_pixel(*camera, Vec3{5, 0, -10});
    auto down = project_world_to_pixel(*camera, Vec3{0, 5, -10});
    expect(origin && right && down, "axis samples");
    if (origin && right && down) {
      expect(right->x > origin->x, "+X increases pixel x");
      expect(down->y > origin->y, "+Y increases pixel y (no invert)");
    }

    // Nearer layer moves more under a camera pan (parallax).
    LocalTransform pan;
    pan.translate = {2, 0, 0};
    auto panned = make_perspective_camera(local_matrix(pan), frustum, viewport,
                                          AspectFit::stretch, kWidth, kHeight);
    expect(panned.has_value(), "panned camera");
    if (panned) {
      const Vec3 near_pt{0, 0, -5};
      const Vec3 far_pt{0, 0, -40};
      auto near_a = project_world_to_pixel(*camera, near_pt);
      auto near_b = project_world_to_pixel(*panned, near_pt);
      auto far_a = project_world_to_pixel(*camera, far_pt);
      auto far_b = project_world_to_pixel(*panned, far_pt);
      expect(near_a && near_b && far_a && far_b, "parallax samples");
      if (near_a && near_b && far_a && far_b) {
        const double near_dx = std::fabs(near_b->x - near_a->x);
        const double far_dx = std::fabs(far_b->x - far_a->x);
        expect(near_dx > far_dx, "nearer layer has larger parallax");
        expect(near_dx > 0.0 && far_dx > 0.0, "both layers move");
      }
    }
  }

  {
    // Defined parallax_factor: nearer / focus => factor > 1.
    auto near_factor = parallax_factor(5.0, 10.0);
    auto far_factor = parallax_factor(40.0, 10.0);
    expect(near_factor && far_factor, "parallax factors");
    if (near_factor && far_factor) {
      expect(nearly_equal(*near_factor, 2.0), "near factor 2");
      expect(nearly_equal(*far_factor, 0.25), "far factor 0.25");
      expect(*near_factor > *far_factor, "near factor exceeds far");
    }
    expect(!parallax_factor(0.0, 10.0).has_value(), "reject zero depth");
  }

  {
    expect(classify_view_depth(-1.0, 1.0, 100.0) == DepthClip::inside,
           "inside depth");
    expect(classify_view_depth(-0.5, 1.0, 100.0) == DepthClip::near_clipped,
           "near clip");
    expect(classify_view_depth(-150.0, 1.0, 100.0) == DepthClip::far_clipped,
           "far clip");

    auto camera = make_perspective_camera(Mat4::identity(), frustum, viewport,
                                          AspectFit::stretch, kWidth, kHeight);
    expect(camera.has_value(), "clip camera");
    if (camera) {
      auto near_ndc = project_world_to_ndc(*camera, Vec3{0, 0, -1.0});
      auto far_ndc = project_world_to_ndc(*camera, Vec3{0, 0, -100.0});
      expect(near_ndc && far_ndc, "plane ndc");
      if (near_ndc && far_ndc) {
        expect(nearly_equal(near_ndc->z, -1.0, 1e-6, 1e-6), "near -> ndc -1");
        expect(nearly_equal(far_ndc->z, 1.0, 1e-6, 1e-6), "far -> ndc +1");
        expect(is_inside_ndc(*near_ndc), "near plane inside ndc");
        expect(is_inside_ndc(*far_ndc), "far plane inside ndc");
      }
      auto behind = project_world_to_ndc(*camera, Vec3{0, 0, 5.0});
      // Behind camera: clip.w = -view_z < 0; still produces ndc but depth is
      // outside the view volume classification.
      expect(behind.has_value(), "project behind yields ndc");
      expect(classify_view_depth(5.0, 1.0, 100.0) == DepthClip::near_clipped,
             "positive view_z is near-clipped");
    }
  }

  {
    // Ortho remains available and does not invert axes either.
    auto ortho = canvas_ortho_frustum(100, 200, 1.0, 50.0);
    auto camera =
        make_ortho_camera(Mat4::identity(), *ortho, Viewport{0, 0, 100, 200},
                          AspectFit::stretch, 100, 200);
    expect(camera.has_value(), "ortho still works");
    if (camera) {
      auto a = project_world_to_pixel(*camera, Vec3{10, 20, -5});
      auto b = project_world_to_pixel(*camera, Vec3{30, 40, -5});
      expect(a && b && b->x > a->x && b->y > a->y, "ortho axes stable");
    }
  }

  {
    expect(!perspective_projection(PerspectiveFrustum{0, 1, 1, 10}).has_value(),
           "reject zero fov");
    expect(!perspective_projection(PerspectiveFrustum{1, 0, 1, 10}).has_value(),
           "reject zero aspect");
    expect(!perspective_projection(PerspectiveFrustum{1, 1, 10, 1}).has_value(),
           "reject inverted depth");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
