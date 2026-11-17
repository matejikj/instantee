#pragma once

#include <instantee/core_model/transform.hpp>
#include <instantee/diagnostics/error.hpp>

#include <cstdint>
#include <expected>
#include <string_view>

namespace instantee::core {

// How a canvas aspect maps into a viewport rectangle.
enum class AspectFit : std::uint8_t {
  stretch = 0, // map full frustum to the full viewport (may distort)
  contain      // letterbox/pillarbox; preserve canvas aspect
};

// Pixel-space viewport. Origin is top-left of the destination surface; +Y down.
struct Viewport {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

// Orthographic frustum in canvas/world units. For SAS §8.1 Y-down canvases,
// top is the smaller y (upper edge) and bottom is the larger y.
struct OrthoFrustum {
  double left = 0;
  double right = 0;
  double top = 0;
  double bottom = 0;
  // Positive distances along camera forward (-Z). Near plane at view z=-near,
  // far plane at view z=-far.
  double near_distance = 0.1;
  double far_distance = 1000.0;
};

namespace detail {

inline diagnostics::Error cam_err(std::string_view code,
                                  std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "camera", message};
}

} // namespace detail

// Invert an affine Mat4 (last row 0,0,0,1). Rejects singular upper-left 3×3.
[[nodiscard]] inline std::expected<Mat4, diagnostics::Error>
invert_affine(const Mat4 &m) {
  if (!nearly_equal(m(3, 0), 0.0) || !nearly_equal(m(3, 1), 0.0) ||
      !nearly_equal(m(3, 2), 0.0) || !nearly_equal(m(3, 3), 1.0)) {
    return std::unexpected(
        detail::cam_err("non_affine", "Camera matrix must be affine"));
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
        detail::cam_err("singular_matrix", "Camera matrix is singular"));
  }
  const double inv_det = 1.0 / det;

  Mat4 out = Mat4::identity();
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

// Frustum covering a pixel canvas with (0,0) at the top-left.
[[nodiscard]] inline std::expected<OrthoFrustum, diagnostics::Error>
canvas_ortho_frustum(std::uint32_t canvas_width, std::uint32_t canvas_height,
                     double near_distance = 0.1, double far_distance = 1000.0) {
  if (canvas_width == 0 || canvas_height == 0) {
    return std::unexpected(
        detail::cam_err("invalid_canvas", "Canvas size must be positive"));
  }
  if (!(near_distance > 0.0) || !(far_distance > near_distance) ||
      !std::isfinite(near_distance) || !std::isfinite(far_distance)) {
    return std::unexpected(detail::cam_err(
        "invalid_depth", "near_distance must be > 0 and < far_distance"));
  }
  OrthoFrustum frustum;
  frustum.left = 0;
  frustum.right = static_cast<double>(canvas_width);
  frustum.top = 0;
  frustum.bottom = static_cast<double>(canvas_height);
  frustum.near_distance = near_distance;
  frustum.far_distance = far_distance;
  return frustum;
}

// Orthographic projection. Maps frustum to clip/NDC with Y-down preserved
// (increasing canvas y increases NDC y). View-space z=-near → NDC z=-1,
// z=-far → NDC z=+1 (camera forward = -Z).
[[nodiscard]] inline std::expected<Mat4, diagnostics::Error>
ortho_projection(const OrthoFrustum &frustum) {
  const double w = frustum.right - frustum.left;
  const double h = frustum.bottom - frustum.top;
  const double depth = frustum.far_distance - frustum.near_distance;
  if (!(w > 0.0) || !(h > 0.0) || !(depth > 0.0) || !std::isfinite(w) ||
      !std::isfinite(h) || !std::isfinite(depth)) {
    return std::unexpected(
        detail::cam_err("invalid_frustum",
                        "Ortho frustum extents must be finite and positive"));
  }
  if (!(frustum.near_distance > 0.0)) {
    return std::unexpected(
        detail::cam_err("invalid_depth", "near_distance must be positive"));
  }

  Mat4 out{};
  out(0, 0) = 2.0 / w;
  out(1, 1) = 2.0 / h;
  // view_z in [-far, -near] → ndc_z in [-1, 1]
  out(2, 2) = -2.0 / depth;
  out(0, 3) = -(frustum.right + frustum.left) / w;
  out(1, 3) = -(frustum.bottom + frustum.top) / h;
  out(2, 3) = -(frustum.far_distance + frustum.near_distance) / depth;
  out(3, 3) = 1.0;
  return out;
}

// NDC [-1,1] → pixel coordinates inside the viewport (Y-down).
[[nodiscard]] inline std::expected<Mat4, diagnostics::Error>
viewport_matrix(const Viewport &viewport) {
  if (!(viewport.width > 0.0) || !(viewport.height > 0.0) ||
      !std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
      !std::isfinite(viewport.width) || !std::isfinite(viewport.height)) {
    return std::unexpected(detail::cam_err(
        "invalid_viewport", "Viewport size must be finite and positive"));
  }
  Mat4 out = Mat4::identity();
  out(0, 0) = viewport.width * 0.5;
  out(1, 1) = viewport.height * 0.5;
  out(0, 3) = viewport.x + viewport.width * 0.5;
  out(1, 3) = viewport.y + viewport.height * 0.5;
  return out;
}

// View = inverse(camera_world). Camera looks along its local -Z.
[[nodiscard]] inline std::expected<Mat4, diagnostics::Error>
view_from_camera_world(const Mat4 &camera_world) {
  return invert_affine(camera_world);
}

// Place a content aspect inside an outer viewport.
[[nodiscard]] inline std::expected<Viewport, diagnostics::Error>
fit_viewport(AspectFit fit, double content_width, double content_height,
             const Viewport &outer) {
  if (!(content_width > 0.0) || !(content_height > 0.0) ||
      !std::isfinite(content_width) || !std::isfinite(content_height)) {
    return std::unexpected(detail::cam_err(
        "invalid_canvas", "Content size must be finite and positive"));
  }
  if (!(outer.width > 0.0) || !(outer.height > 0.0)) {
    return std::unexpected(detail::cam_err(
        "invalid_viewport", "Viewport size must be finite and positive"));
  }
  if (fit == AspectFit::stretch) {
    return outer;
  }

  const double content_aspect = content_width / content_height;
  const double outer_aspect = outer.width / outer.height;
  Viewport fitted = outer;
  if (content_aspect > outer_aspect) {
    // Pillarbox: limited by width.
    fitted.height = outer.width / content_aspect;
    fitted.y = outer.y + (outer.height - fitted.height) * 0.5;
  } else {
    // Letterbox: limited by height.
    fitted.width = outer.height * content_aspect;
    fitted.x = outer.x + (outer.width - fitted.width) * 0.5;
  }
  return fitted;
}

struct OrthoCameraMatrices {
  Mat4 view = Mat4::identity();
  Mat4 projection = Mat4::identity();
  Mat4 view_projection = Mat4::identity();
  Mat4 viewport = Mat4::identity();
  // pixel_from_world = viewport * projection * view
  Mat4 pixel_from_world = Mat4::identity();
  Viewport fitted_viewport{};
};

[[nodiscard]] inline std::expected<OrthoCameraMatrices, diagnostics::Error>
make_ortho_camera(const Mat4 &camera_world, const OrthoFrustum &frustum,
                  const Viewport &outer_viewport, AspectFit fit,
                  double content_width, double content_height) {
  auto fitted =
      fit_viewport(fit, content_width, content_height, outer_viewport);
  if (!fitted) {
    return std::unexpected(fitted.error());
  }
  auto view = view_from_camera_world(camera_world);
  if (!view) {
    return std::unexpected(view.error());
  }
  auto projection = ortho_projection(frustum);
  if (!projection) {
    return std::unexpected(projection.error());
  }
  auto viewport = viewport_matrix(*fitted);
  if (!viewport) {
    return std::unexpected(viewport.error());
  }

  OrthoCameraMatrices out;
  out.view = *view;
  out.projection = *projection;
  out.view_projection = mul(*projection, *view);
  out.viewport = *viewport;
  out.pixel_from_world = mul(*viewport, out.view_projection);
  out.fitted_viewport = *fitted;
  return out;
}

// Returns pixel x/y in the fitted viewport. z is NDC depth after projection
// (not a framebuffer pixel coordinate).
[[nodiscard]] inline std::expected<Vec3, diagnostics::Error>
project_world_to_pixel(const OrthoCameraMatrices &camera, Vec3 world_point) {
  const Vec4 clip = mul(camera.view_projection,
                        Vec4{world_point.x, world_point.y, world_point.z, 1.0});
  if (!std::isfinite(clip.w) || std::fabs(clip.w) < 1e-12) {
    return std::unexpected(
        detail::cam_err("invalid_clip", "Clip W is zero or non-finite"));
  }
  const Vec3 ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
  return transform_point(camera.viewport, ndc);
}

} // namespace instantee::core
