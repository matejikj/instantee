#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/render_graph.hpp>

#include <cstdint>
#include <expected>
#include <vector>

namespace instantee::render_core {

// Nested clip/mask script for one offscreen color target. Scissor layers are
// axis-aligned AABB intersections (fast path). Shape-mask layers AND coverage
// from fill_* DrawOps (stencil/offscreen mask path). Domain-neutral: no Metal.
enum class ClipCommandKind : std::uint8_t {
  push_scissor = 1,
  push_shape_mask = 2,
  pop = 3,
  draw = 4,
};

struct ClipCommand {
  ClipCommandKind kind = ClipCommandKind::draw;
  // push_scissor: pixel-space AABB (origin top-left).
  Rect2f scissor{};
  // push_shape_mask: fill_* shape whose coverage becomes a visibility mask.
  // draw: clear or shape under the active clip/mask stack.
  DrawOp op{};
};

struct ClipScript {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::vector<ClipCommand> commands{};
  // Hard cap on nested push depth (scissors + masks combined).
  static constexpr std::uint32_t kMaxDepth = 8;
};

struct ClipStackStats {
  std::uint32_t max_depth = 0;
  std::uint32_t scissor_pushes = 0;
  std::uint32_t mask_pushes = 0;
  std::uint32_t pops = 0;
  std::uint32_t draws = 0;
};

// Pixel-aligned inclusive-min / exclusive-max scissor used by backends.
struct PixelScissor {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

[[nodiscard]] constexpr bool is_empty_scissor(const PixelScissor &s) noexcept {
  return s.width <= 0 || s.height <= 0;
}

// Intersect two pixel scissors. Empty when no overlap.
[[nodiscard]] PixelScissor intersect_scissors(const PixelScissor &a,
                                              const PixelScissor &b) noexcept;

// Convert a float AABB to a clamped pixel scissor inside the pass.
[[nodiscard]] PixelScissor scissor_from_rect(const Rect2f &rect,
                                             std::uint32_t width_px,
                                             std::uint32_t height_px) noexcept;

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_clip_script(const ClipScript &script);

// CPU reference compositor: nested scissors AND shape masks. clear ignores
// masks (full target replace) but still requires an empty stack so layering
// stays explicit. Output origin top-left, straight RGBA8.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_clip_script(const ClipScript &script);

[[nodiscard]] std::expected<ClipStackStats, diagnostics::Error>
clip_script_stats(const ClipScript &script);

} // namespace instantee::render_core
