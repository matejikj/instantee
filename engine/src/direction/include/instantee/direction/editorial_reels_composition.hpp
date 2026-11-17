#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/safe_area.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Editorial Reels composition presets for top headline, callout, chart, and
// bottom space. Slot geometry is driven by content-safe ratios and Style Bible
// spacing tokens — not hard-coded canvas pixel constants. Safe-area insets come
// from the versioned Style Bible token document.

enum class ReelsCompositionSlot : std::uint8_t {
  top_headline = 1,
  callout = 2,
  chart = 3,
  bottom_space = 4,
};

// Height shares of the content-safe rect (after gaps). Must be finite, > 0, and
// sum to less than 1 so the bottom band receives the remainder.
struct ReelsCompositionRatios {
  double top_headline = 0.18;
  double callout = 0.12;
  double chart = 0.40;
  // Callout width share of the content width (right-aligned editorial chip).
  double callout_width = 0.42;
};

struct ReelsCompositionRequest {
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
  SafeAreaPolicy safe_area{};
  // Vertical gap between stacked slots; sourced from spacing tokens.
  double stack_gap_px = 16.0;
  // Horizontal inset used when callout is narrower than the content width.
  double gutter_px = 24.0;
  ReelsCompositionRatios ratios{};
  // Provenance of the versioned style token that supplied safe-area/spacing.
  std::string style_package_id{};
  std::string style_version{};
};

struct ReelsCompositionLayout {
  std::vector<LayoutElement> elements{};
  ContentSafeRect content{};
  std::string style_package_id{};
  std::string style_version{};
};

[[nodiscard]] constexpr std::string_view
reels_composition_slot_id(ReelsCompositionSlot slot) noexcept {
  switch (slot) {
  case ReelsCompositionSlot::top_headline:
    return "top_headline";
  case ReelsCompositionSlot::callout:
    return "callout";
  case ReelsCompositionSlot::chart:
    return "chart";
  case ReelsCompositionSlot::bottom_space:
    return "bottom_space";
  }
  return "unknown";
}

// Fill safe-area + spacing from a Style Bible. Changing the bible version /
// safe_area token changes the layout without recompiling the engine.
[[nodiscard]] ReelsCompositionRequest
reels_composition_request_from_style(const StyleBible &bible,
                                     double canvas_width_px = 1080.0,
                                     double canvas_height_px = 1920.0);

[[nodiscard]] std::expected<ReelsCompositionLayout, diagnostics::Error>
compose_editorial_reels(const ReelsCompositionRequest &request);

// True when every composed element lies inside the content-safe rect.
[[nodiscard]] bool
reels_composition_respects_safe_area(const ReelsCompositionLayout &layout);

} // namespace instantee::direction
