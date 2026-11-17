#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/style_bible.hpp>

#include <expected>
#include <span>
#include <string_view>

namespace instantee::direction {

// Axis-aligned content rectangle after applying Style Bible safe-area insets.
struct ContentSafeRect {
  double x_px = 0;
  double y_px = 0;
  double width_px = 0;
  double height_px = 0;
};

// UI chrome bands reserved by Reels overlays (top/bottom/side). Empty when the
// policy mode is full_frame.
struct UiSafeRegions {
  ContentSafeRect top{};
  ContentSafeRect bottom{};
  ContentSafeRect left{};
  ContentSafeRect right{};
  bool has_overlay_bands = false;
};

struct SafeAreaLayoutOptions {
  // Elements with importance >= threshold (or keep_visible) must stay inside
  // the content rect for reels_overlay. reels_clear protects all elements.
  double important_threshold = 0.7;
};

// Derive the usable content rect from a data-driven SafeAreaPolicy. Changing
// Style Bible insets does not require recompiling the engine.
[[nodiscard]] std::expected<ContentSafeRect, diagnostics::Error>
content_safe_rect(const SafeAreaPolicy &policy, double canvas_width_px,
                  double canvas_height_px);

[[nodiscard]] std::expected<UiSafeRegions, diagnostics::Error>
ui_safe_regions(const SafeAreaPolicy &policy, double canvas_width_px,
                double canvas_height_px);

// Clamp protected layout elements into the content safe rect via the
// composition solver. Important content must not overlap UI gutters.
[[nodiscard]] std::expected<CompositionSolution, diagnostics::Error>
enforce_safe_area(std::span<const LayoutElement> elements,
                  const SafeAreaPolicy &policy, double canvas_width_px,
                  double canvas_height_px, SafeAreaLayoutOptions options = {});

[[nodiscard]] constexpr std::string_view
safe_area_mode_id(SafeAreaMode mode) noexcept {
  switch (mode) {
  case SafeAreaMode::reels_overlay:
    return "reels_overlay";
  case SafeAreaMode::reels_clear:
    return "reels_clear";
  case SafeAreaMode::full_frame:
    return "full_frame";
  }
  return "unknown";
}

} // namespace instantee::direction
