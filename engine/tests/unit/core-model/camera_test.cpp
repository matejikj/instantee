#include <instantee/core_model/camera.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool same_pixel_xy(instantee::core::Vec3 actual,
                   instantee::core::Vec3 expected) {
  return instantee::core::nearly_equal(actual.x, expected.x, 1e-6, 1e-6) &&
         instantee::core::nearly_equal(actual.y, expected.y, 1e-6, 1e-6);
}

} // namespace

int main() {
  using namespace instantee::core;

  constexpr std::uint32_t kWidth = 1080;
  constexpr std::uint32_t kHeight = 1920;

  {
    auto frustum = canvas_ortho_frustum(kWidth, kHeight);
    expect(frustum.has_value(), "canvas frustum");
    if (!frustum) {
      return EXIT_FAILURE;
    }
    Viewport viewport{0, 0, static_cast<double>(kWidth),
                      static_cast<double>(kHeight)};
    auto camera = make_ortho_camera(Mat4::identity(), *frustum, viewport,
                                    AspectFit::stretch, kWidth, kHeight);
    expect(camera.has_value(), "identity ortho camera");
    if (!camera) {
      return EXIT_FAILURE;
    }

    // Golden pixel layout: world canvas coords equal destination pixels.
    const struct {
      Vec3 world;
      Vec3 pixel;
      const char *label;
    } samples[] = {
        {{0, 0, 0}, {0, 0, 0}, "origin"},
        {{100, 200, 0}, {100, 200, 0}, "text sample"},
        {{540, 960, 0}, {540, 960, 0}, "center"},
        {{1080, 1920, 0}, {1080, 1920, 0}, "bottom-right"},
    };
    for (const auto &sample : samples) {
      auto pixel = project_world_to_pixel(*camera, sample.world);
      expect(pixel.has_value(), sample.label);
      if (pixel) {
        expect(same_pixel_xy(*pixel, sample.pixel), sample.label);
      }
    }
  }

  {
    // Camera translated by (+10,+20): world (10,20) maps to pixel (0,0).
    LocalTransform cam_local;
    cam_local.translate = {10, 20, 0};
    const Mat4 cam_world = local_matrix(cam_local);
    auto frustum = canvas_ortho_frustum(200, 100);
    expect(frustum.has_value(), "small frustum");
    Viewport viewport{0, 0, 200, 100};
    auto camera = make_ortho_camera(cam_world, *frustum, viewport,
                                    AspectFit::stretch, 200, 100);
    expect(camera.has_value(), "translated camera");
    if (camera) {
      auto pixel = project_world_to_pixel(*camera, Vec3{10, 20, 0});
      expect(pixel.has_value() && same_pixel_xy(*pixel, Vec3{0, 0, 0}),
             "translated camera origin");
      auto pixel2 = project_world_to_pixel(*camera, Vec3{30, 40, 0});
      expect(pixel2.has_value() && same_pixel_xy(*pixel2, Vec3{20, 20, 0}),
             "translated camera offset");
    }
  }

  {
    // Contain: 100x200 content in a 200x200 viewport → width 100, centered.
    Viewport outer{0, 0, 200, 200};
    auto fitted = fit_viewport(AspectFit::contain, 100, 200, outer);
    expect(fitted.has_value(), "contain fit");
    if (fitted) {
      expect(nearly_equal(fitted->width, 100.0), "contain width");
      expect(nearly_equal(fitted->height, 200.0), "contain height");
      expect(nearly_equal(fitted->x, 50.0), "contain x inset");
      expect(nearly_equal(fitted->y, 0.0), "contain y");
    }

    auto frustum = canvas_ortho_frustum(100, 200);
    auto camera = make_ortho_camera(Mat4::identity(), *frustum, outer,
                                    AspectFit::contain, 100, 200);
    expect(camera.has_value(), "contain camera");
    if (camera) {
      auto origin = project_world_to_pixel(*camera, Vec3{0, 0, 0});
      auto corner = project_world_to_pixel(*camera, Vec3{100, 200, 0});
      expect(origin.has_value() && same_pixel_xy(*origin, Vec3{50, 0, 0}),
             "contain maps origin to letterbox");
      expect(corner.has_value() && same_pixel_xy(*corner, Vec3{150, 200, 0}),
             "contain maps corner");
    }
  }

  {
    expect(!canvas_ortho_frustum(0, 100).has_value(), "reject zero width");
    expect(!ortho_projection(OrthoFrustum{0, 0, 0, 1, 0.1, 1}).has_value(),
           "reject zero frustum width");
    OrthoFrustum bad_depth = *canvas_ortho_frustum(10, 10);
    bad_depth.near_distance = 10;
    bad_depth.far_distance = 1;
    expect(!ortho_projection(bad_depth).has_value(), "reject inverted depth");
    expect(!viewport_matrix(Viewport{0, 0, 0, 10}).has_value(),
           "reject zero viewport");
  }

  {
    // Depth: point at view z=-near projects to NDC z=-1 → viewport leaves z.
    auto frustum = canvas_ortho_frustum(100, 100, 1.0, 11.0);
    Viewport viewport{0, 0, 100, 100};
    auto camera = make_ortho_camera(Mat4::identity(), *frustum, viewport,
                                    AspectFit::stretch, 100, 100);
    expect(camera.has_value(), "depth camera");
    if (camera) {
      auto near_pt = project_world_to_pixel(*camera, Vec3{50, 50, -1.0});
      auto far_pt = project_world_to_pixel(*camera, Vec3{50, 50, -11.0});
      expect(near_pt.has_value() && nearly_equal(near_pt->z, -1.0, 1e-6, 1e-6),
             "near plane ndc z");
      expect(far_pt.has_value() && nearly_equal(far_pt->z, 1.0, 1e-6, 1e-6),
             "far plane ndc z");
    }
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
