#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstdint>
#include <expected>
#include <string_view>
#include <variant>
#include <vector>

namespace instantee::render_core {

// Editorial graphic vocabulary lowered onto shared DrawOp primitives only.
// No Metal types and no new renderer — backends keep consuming DrawOp/PassDesc.

enum class GraphicShapeKind : std::uint8_t {
  arrow = 1,
  technical_line = 2,
  outline = 3,
  stamp = 4,
  numeric_callout = 5,
  highlight = 6,
};

struct ArrowShape {
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  float shaft_width = 2.0f;
  float head_length = 10.0f;
  float head_width = 8.0f;
  Rgba8 color{0, 0, 0, 255};
};

struct TechnicalLineShape {
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  float stroke_width = 1.5f;
  // 0 disables ticks. Otherwise spacing along the segment in pixels.
  float tick_spacing = 0;
  float tick_length = 4.0f;
  Rgba8 color{0, 0, 0, 255};
};

struct OutlineShape {
  Rect2f bounds{};
  float stroke_width = 2.0f;
  Rgba8 color{0, 0, 0, 255};
};

struct StampShape {
  Rect2f bounds{};
  float corner_radius = 4.0f;
  float ring_inset_px = 3.0f;
  Rgba8 fill{198, 255, 0, 255};
  Rgba8 ring{0, 0, 0, 255};
};

struct NumericCalloutShape {
  float cx = 0;
  float cy = 0;
  float radius = 12.0f;
  // Digit rendered as a 7-segment stroke figure inside the badge (0–9).
  std::uint8_t digit = 0;
  // Optional stem tip; when equal to (cx,cy) the stem is omitted.
  float stem_x = 0;
  float stem_y = 0;
  Rgba8 fill{0, 0, 0, 255};
  Rgba8 ink{255, 255, 255, 255};
  Rgba8 stem_color{0, 0, 0, 255};
};

struct HighlightShape {
  Rect2f bounds{};
  float corner_radius = 0;
  Rgba8 color{198, 255, 0, 96};
};

using GraphicShape =
    std::variant<ArrowShape, TechnicalLineShape, OutlineShape, StampShape,
                 NumericCalloutShape, HighlightShape>;

struct GraphicShapeScene {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  Rgba8 clear_color{255, 255, 255, 255};
  std::vector<GraphicShape> shapes{};
};

[[nodiscard]] constexpr std::string_view
graphic_shape_kind_id(GraphicShapeKind kind) noexcept {
  switch (kind) {
  case GraphicShapeKind::arrow:
    return "arrow";
  case GraphicShapeKind::technical_line:
    return "technical_line";
  case GraphicShapeKind::outline:
    return "outline";
  case GraphicShapeKind::stamp:
    return "stamp";
  case GraphicShapeKind::numeric_callout:
    return "numeric_callout";
  case GraphicShapeKind::highlight:
    return "highlight";
  }
  return "unknown";
}

[[nodiscard]] GraphicShapeKind
graphic_shape_kind(const GraphicShape &shape) noexcept;

// Lower one component to DrawOps. Rejects non-finite geometry and empty
// extents.
[[nodiscard]] std::expected<std::vector<DrawOp>, diagnostics::Error>
lower_graphic_shape(const GraphicShape &shape);

// Compose clear + all shapes into a single PassDesc over shared primitives.
[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
compose_graphic_shape_pass(const GraphicShapeScene &scene);

// CPU snapshot for component goldens via existing rasterize_pass.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_graphic_shape_scene(const GraphicShapeScene &scene);

} // namespace instantee::render_core
