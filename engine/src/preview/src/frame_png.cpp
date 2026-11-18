#include <instantee/preview/frame_png.hpp>

#include <instantee/core_model/color.hpp>
#include <instantee/core_model/time.hpp>
#include <instantee/render_core/frame_primitives.hpp>
#include <instantee/render_core/linear_compositor.hpp>
#include <instantee/render_core/visibility.hpp>
#include <instantee/runtime/layering.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace instantee::preview {
namespace {

using render_core::DrawOp;
using render_core::DrawOpKind;
using render_core::encode_rec709_rgba8;
using render_core::LinearCompositeScene;
using render_core::LinearLayer;
using render_core::lower_primitives_to_pass;
using render_core::rasterize_linear_composite;
using render_core::Rect2f;
using render_core::Rgba8;
using render_core::Rgba8Image;
using render_core::SceneDrawItem;
using render_core::WorldPrimitive;

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "preview_frame", message};
}

[[nodiscard]] bool is_rec709_canvas(std::string_view label) noexcept {
  return label == "rec709-sdr" || label == "rec709" || label == "srgb";
}

[[nodiscard]] Rgba8 linear_to_srgb8(core::LinearRgba c) noexcept {
  const double a = std::clamp(c.a, 0.0, 1.0);
  return Rgba8{
      static_cast<std::uint8_t>(
          std::lround(core::linear_channel_to_srgb(c.r) * 255.0)),
      static_cast<std::uint8_t>(
          std::lround(core::linear_channel_to_srgb(c.g) * 255.0)),
      static_cast<std::uint8_t>(
          std::lround(core::linear_channel_to_srgb(c.b) * 255.0)),
      static_cast<std::uint8_t>(std::lround(a * 255.0)),
  };
}

[[nodiscard]] std::array<float, 16>
mat4_to_float(const core::Mat4 &m) noexcept {
  std::array<float, 16> out{};
  for (std::size_t col = 0; col < 4; ++col) {
    for (std::size_t row = 0; row < 4; ++row) {
      out[col * 4U + row] = static_cast<float>(m(row, col));
    }
  }
  return out;
}

[[nodiscard]] Rect2f proxy_local_bounds(const runtime::CompiledScene &scene,
                                        const runtime::RuntimeNode &node) {
  if (node.kind == runtime::RuntimeNodeKind::text &&
      node.text_run_index.has_value()) {
    const auto runs = scene.text_runs();
    const std::uint32_t idx = *node.text_run_index;
    if (idx < runs.size()) {
      const runtime::RuntimeTextRun &run = runs[idx];
      const float size = static_cast<float>(std::max(run.font_size_px, 1.0));
      const float width =
          size *
          static_cast<float>(std::max<std::size_t>(run.text.size(), 1U)) * 0.6f;
      return Rect2f{0, 0, std::max(width, size), size * 1.2f};
    }
  }
  // Geometry/image/puppet placeholder until mesh/atlas payloads are wired.
  return Rect2f{0, 0, 64, 64};
}

[[nodiscard]] bool is_drawable(runtime::RuntimeNodeKind kind) noexcept {
  switch (kind) {
  case runtime::RuntimeNodeKind::geometry:
  case runtime::RuntimeNodeKind::text:
  case runtime::RuntimeNodeKind::image:
  case runtime::RuntimeNodeKind::puppet:
    return true;
  case runtime::RuntimeNodeKind::group:
  case runtime::RuntimeNodeKind::camera:
    return false;
  }
  return false;
}

} // namespace

std::expected<Rgba8Image, diagnostics::Error>
rasterize_preview_frame(const runtime::CompiledScene &scene,
                        const runtime::FrameState &state,
                        const FramePngRequest &request) {
  if (request.color_space != PreviewColorSpace::rec709_sdr) {
    return std::unexpected(
        make_invalid("unsupported_preview_color_space",
                     "preview raster currently supports only rec709-sdr"));
  }
  if (!is_rec709_canvas(scene.canvas_color())) {
    return std::unexpected(make_invalid(
        "unsupported_canvas_color",
        "preview PNG requires a rec709-sdr (or srgb) canvas color label"));
  }
  if (scene.canvas_width() == 0 || scene.canvas_height() == 0) {
    return std::unexpected(make_invalid("invalid_canvas_extent",
                                        "canvas width/height must be > 0"));
  }
  if (state.nodes.size() != scene.nodes().size()) {
    return std::unexpected(
        make_invalid("frame_state_mismatch",
                     "FrameState node count must match CompiledScene"));
  }

  const Rgba8 clear = request.clear_color.value_or(Rgba8{0, 0, 0, 255});

  // Emit draw items in runtime paint order: opaque batch then transparent.
  std::vector<WorldPrimitive> prims;
  prims.reserve(state.draw_order.opaque_batch.size() +
                state.draw_order.transparent_paint.size());

  auto append_item = [&](const runtime::DrawItem &item) -> bool {
    if (item.node_index >= state.nodes.size()) {
      return false;
    }
    const runtime::RuntimeNode &node = scene.nodes()[item.node_index];
    const runtime::NodeFrameState &node_state = state.nodes[item.node_index];
    if (!is_drawable(node.kind) || !node_state.inherited_visible ||
        node_state.clip_empty) {
      return true;
    }
    // Fold draw-item opacity into the authored alpha channel. Fully
    // transparent proxies are skipped so fade-in frames do not pay for a
    // full-resolution composite of invisible layers.
    const double opacity = std::clamp(item.opacity, 0.0, 1.0);
    if (opacity <= 0.0) {
      return true;
    }
    WorldPrimitive prim;
    prim.kind = DrawOpKind::fill_rect;
    prim.local_bounds = proxy_local_bounds(scene, node);
    prim.color = linear_to_srgb8(node_state.material_color);
    prim.color.a = static_cast<std::uint8_t>(
        std::lround(static_cast<double>(prim.color.a) * opacity));
    if (prim.color.a == 0) {
      return true;
    }
    prim.world_col_major = mat4_to_float(node_state.world);
    prims.push_back(prim);
    return true;
  };

  for (const runtime::DrawItem &item : state.draw_order.opaque_batch) {
    if (!append_item(item)) {
      return std::unexpected(make_invalid("invalid_draw_item",
                                          "draw item node index out of range"));
    }
  }
  for (const runtime::DrawItem &item : state.draw_order.transparent_paint) {
    if (!append_item(item)) {
      return std::unexpected(make_invalid("invalid_draw_item",
                                          "draw item node index out of range"));
    }
  }

  auto pass =
      lower_primitives_to_pass(prims, render_core::LowerPrimitivesConfig{
                                          .width_px = scene.canvas_width(),
                                          .height_px = scene.canvas_height(),
                                          .color_target = 1,
                                          .clear_color = clear,
                                      });
  if (!pass) {
    return std::unexpected(pass.error());
  }

  // Rebuild as a linear composite so semi-transparent proxies use the Rec.709
  // encode path (matches the production color contract).
  LinearCompositeScene linear;
  linear.width_px = pass->width_px;
  linear.height_px = pass->height_px;
  linear.clear_color = clear;
  for (const DrawOp &op : pass->ops) {
    if (op.kind == DrawOpKind::clear) {
      continue;
    }
    linear.layers.push_back(LinearLayer{.shape = op, .opacity = 1.0f});
  }
  auto solid_clear = [&]() -> Rgba8Image {
    Rgba8Image image;
    image.width_px = linear.width_px;
    image.height_px = linear.height_px;
    image.rgba.assign(static_cast<std::size_t>(linear.width_px) *
                          static_cast<std::size_t>(linear.height_px) * 4U,
                      0);
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
      image.rgba[i] = clear.r;
      image.rgba[i + 1] = clear.g;
      image.rgba[i + 2] = clear.b;
      image.rgba[i + 3] = clear.a;
    }
    return image;
  };

  if (linear.layers.empty()) {
    return solid_clear();
  }

  // Cull fully offscreen proxies before compositing.
  std::vector<SceneDrawItem> cull_items;
  cull_items.reserve(linear.layers.size());
  for (std::size_t i = 0; i < linear.layers.size(); ++i) {
    cull_items.push_back(SceneDrawItem{
        .op = linear.layers[i].shape,
        .visible = true,
        .transparent = linear.layers[i].shape.color.a < 255,
        .stable_key = static_cast<std::uint64_t>(i),
    });
  }
  auto culled = render_core::cull_and_sort_draws(
      cull_items,
      render_core::Viewport2f{0, 0, static_cast<float>(linear.width_px),
                              static_cast<float>(linear.height_px)});
  if (!culled) {
    return std::unexpected(culled.error());
  }
  linear.layers.clear();
  for (const DrawOp &op : culled->draws) {
    linear.layers.push_back(LinearLayer{.shape = op, .opacity = 1.0f});
  }
  if (linear.layers.empty()) {
    return solid_clear();
  }

  auto working = rasterize_linear_composite(linear);
  if (!working) {
    return std::unexpected(working.error());
  }
  return encode_rec709_rgba8(*working);
}

std::expected<runtime::FramePreview, diagnostics::Error>
export_preview_png(const runtime::CompiledScene &scene,
                   const runtime::AnimationProgram &program,
                   core::FrameIndex at, const std::filesystem::path &png_path,
                   const FramePngRequest &request) {
  auto preview = runtime::preview_frame_at(scene, program, at);
  if (!preview) {
    return std::unexpected(preview.error());
  }
  auto image = rasterize_preview_frame(scene, preview->state, request);
  if (!image) {
    return std::unexpected(image.error());
  }
  if (auto written =
          write_rgba8_png(png_path, image->width_px, image->height_px,
                          image->rgba, request.color_space);
      !written) {
    return std::unexpected(written.error());
  }
  return preview;
}

} // namespace instantee::preview
