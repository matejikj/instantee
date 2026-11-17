#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace instantee::render_core {

// Pass-space viewport used for frustum/scissor culling. Origin top-left.
struct Viewport2f {
  float x = 0;
  float y = 0;
  float width = 0;
  float height = 0;
};

// One drawable contribution already expressed in pass pixel space. Sort keys
// mirror the runtime draw-order contract so alpha order stays deterministic
// without pulling runtime types into render-core.
struct SceneDrawItem {
  DrawOp op{};
  bool visible = true;
  // When true, item participates in the transparent back-to-front pass.
  // Opaque items are emitted first (stable), then transparent items.
  bool transparent = false;
  std::int32_t layer_order = 0;
  // Smaller depth_z is farther (drawn earlier among transparent items).
  double depth_z = 0.0;
  // Deterministic tie-break (typically node index / stable id hash).
  std::uint64_t stable_key = 0;
  // Optional additional clip in pass space. Combined with the viewport via
  // intersection; an empty result culls the item.
  std::optional<Rect2f> clip{};
};

struct CullSortStats {
  std::uint32_t candidates = 0;
  std::uint32_t culled_invisible = 0;
  std::uint32_t culled_empty_clip = 0;
  std::uint32_t culled_offscreen = 0;
  std::uint32_t kept = 0;
};

struct CullSortResult {
  // Sorted draw ops only (never includes clear). Painter order: opaque batch
  // then transparent back-to-front.
  std::vector<DrawOp> draws{};
  CullSortStats stats{};
};

// Axis-aligned bounds of a shape DrawOp in pass space. Clears and unknown
// kinds yield an empty optional.
[[nodiscard]] std::optional<Rect2f> draw_op_bounds(const DrawOp &op) noexcept;

[[nodiscard]] constexpr bool is_valid_viewport(const Viewport2f &vp) noexcept {
  return vp.width > 0.0f && vp.height > 0.0f;
}

// Positive-area intersection, or nullopt when empty/non-finite.
[[nodiscard]] std::optional<Rect2f> intersect_rects(const Rect2f &a,
                                                    const Rect2f &b) noexcept;

// True when the axis-aligned bounds share a positive-area overlap.
[[nodiscard]] bool rects_overlap(const Rect2f &a, const Rect2f &b) noexcept;

// Cull invisible / empty-clip / fully offscreen items, then sort. Safe cull:
// only drops draws that cannot cover any viewport pixel. Partially visible
// items are kept intact (no scissor rewrite in this step).
[[nodiscard]] std::expected<CullSortResult, diagnostics::Error>
cull_and_sort_draws(std::span<const SceneDrawItem> items,
                    const Viewport2f &viewport);

// Build a PassDesc: clear + culled/sorted draws. Rejects clear ops inside
// items. `viewport` must be origin (0, 0) so pass pixel space matches bounds.
[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
build_culled_pass(std::span<const SceneDrawItem> items,
                  const Viewport2f &viewport, std::uint64_t color_target,
                  Rgba8 clear_color, CullSortStats *stats = nullptr);

// Geometric offscreen cull of an existing pass while preserving relative order
// of kept shape ops. Prefix clears are retained (last prefix clear wins as the
// pass clear). Safe: pixels match the unculled pass when culled ops were fully
// outside the pass extent.
[[nodiscard]] std::expected<PassDesc, diagnostics::Error>
cull_offscreen_pass(const PassDesc &pass, CullSortStats *stats = nullptr);

} // namespace instantee::render_core
