#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/ink_stroke.hpp>
#include <instantee/render_core/linear_compositor.hpp>
#include <instantee/render_core/shader_features.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace instantee::render_core {

// Notebook accessories lower onto shared DrawOps and composite exclusively
// through the linear-light / premultiplied path (authored colors stay
// sRGB/straight until srgb8_straight_to_linear_premul).

enum class MarkerDrawPreset : std::uint8_t {
  highlighter_pen = 1, // wide, translucent wash
  felt_tip = 2,        // medium opaque ink
  pencil_thin = 3,     // thin graphite-like stroke
};

struct AccessoryPoint {
  float x = 0;
  float y = 0;
};

struct TapeStripSpec {
  std::string id{};
  Rect2f bounds{};
  float corner_radius = 2.0f;
  // Authored sRGB/straight. Typical tape is semi-transparent.
  Rgba8 color{255, 230, 140, 140};
  float opacity = 1.0f;
};

struct PinSpec {
  std::string id{};
  float cx = 0;
  float cy = 0;
  float head_radius = 4.0f;
  float needle_length = 10.0f;
  Rgba8 head_color{200, 40, 50, 255};
  Rgba8 needle_color{90, 90, 100, 255};
  float opacity = 1.0f;
};

struct UnderlineSpec {
  std::string id{};
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  float stroke_width = 2.0f;
  Rgba8 color{30, 50, 120, 220};
  float opacity = 1.0f;
};

struct InkHighlightSpec {
  std::string id{};
  Rect2f bounds{};
  float corner_radius = 2.0f;
  Rgba8 color{255, 230, 60, 96};
  float opacity = 1.0f;
};

struct MarkerStrokeSpec {
  std::string id{};
  std::vector<AccessoryPoint> points{};
  MarkerDrawPreset preset = MarkerDrawPreset::felt_tip;
  // Optional color override; when a=0 the preset default is used.
  Rgba8 color{0, 0, 0, 0};
  // Polyline reveal in [0, 1] (fraction of total segment length).
  float progress = 1.0f;
  float opacity = 1.0f;
};

using NotebookAccessory = std::variant<TapeStripSpec, PinSpec, UnderlineSpec,
                                       InkHighlightSpec, MarkerStrokeSpec>;

struct NotebookAccessoryScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Paper/base clear — authored sRGB/straight, imported via linear compositor.
  Rgba8 clear_color{250, 245, 230, 255};
  // Optional opaque paper plate drawn before accessories (sRGB/straight).
  std::vector<DrawOp> base_ops{};
  std::vector<NotebookAccessory> accessories{};
};

struct MarkerPresetStyle {
  float stroke_width = 2.0f;
  Rgba8 color{20, 40, 90, 255};
  bool uses_ink_stroke_feature = false;
};

[[nodiscard]] constexpr std::string_view
marker_draw_preset_id(MarkerDrawPreset preset) noexcept {
  switch (preset) {
  case MarkerDrawPreset::highlighter_pen:
    return "highlighter_pen";
  case MarkerDrawPreset::felt_tip:
    return "felt_tip";
  case MarkerDrawPreset::pencil_thin:
    return "pencil_thin";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view
notebook_accessory_kind_id(const NotebookAccessory &acc) noexcept {
  if (std::holds_alternative<TapeStripSpec>(acc)) {
    return "tape_strip";
  }
  if (std::holds_alternative<PinSpec>(acc)) {
    return "pin";
  }
  if (std::holds_alternative<UnderlineSpec>(acc)) {
    return "underline";
  }
  if (std::holds_alternative<InkHighlightSpec>(acc)) {
    return "ink_highlight";
  }
  return "marker_stroke";
}

[[nodiscard]] MarkerPresetStyle
marker_draw_preset_style(MarkerDrawPreset preset) noexcept;

// Builtins + ink_stroke (shared with animated ink). Accessories do not invent
// a parallel compositing path.
[[nodiscard]] ShaderFeatureRegistry
make_notebook_accessory_shader_feature_registry();

[[nodiscard]] std::expected<std::vector<LinearLayer>, diagnostics::Error>
lower_notebook_accessory(const NotebookAccessory &accessory);

// Builds a LinearCompositeScene: clear → base_ops → accessories in order.
[[nodiscard]] std::expected<LinearCompositeScene, diagnostics::Error>
compose_notebook_accessory_scene(const NotebookAccessoryScene &scene);

// Linear premul composite + Rec.709/sRGB encode for accessory layer goldens.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_notebook_accessory_scene(const NotebookAccessoryScene &scene);

[[nodiscard]] std::string format_notebook_accessory_golden(
    std::string_view label, const NotebookAccessoryScene &scene,
    const LinearCompositeScene &linear, const Rgba8Image &encoded);

} // namespace instantee::render_core
