#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/direction/composition_constraints.hpp>
#include <instantee/direction/style_bible.hpp>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace instantee::direction {

// Parametric asymmetrical editorial grid. Element counts 2, 3, and 5 are the
// accepted verification set; other counts in [2, 5] are also supported.
struct EditorialAsymRequest {
  double canvas_width_px = 1080.0;
  double canvas_height_px = 1920.0;
  SafeAreaPolicy safe_area{};
  double column_gap_px = 32.0;
  double stack_gap_px = 16.0;
  double accent_bar_height_px = 12.0;
  // Primary column share of the content width before the gap (0.5, 0.85].
  double primary_column_ratio = 0.62;
  // Headline may overhang the primary column into the secondary by this
  // fraction of the content width (title bleed).
  double headline_overhang_ratio = 0.12;
  std::uint32_t element_count = 3;
};

struct EditorialFocalPoint {
  double x_px = 0.0;
  double y_px = 0.0;
  // Axis-aligned band that must contain the focal point for readability.
  double band_x_px = 0.0;
  double band_y_px = 0.0;
  double band_width_px = 0.0;
  double band_height_px = 0.0;
  bool inside_readable_band = false;
};

struct EditorialAsymLayout {
  std::vector<LayoutElement> elements;
  EditorialFocalPoint focal{};
  double primary_column_x_px = 0.0;
  double primary_column_width_px = 0.0;
  double secondary_column_x_px = 0.0;
  double secondary_column_width_px = 0.0;
  double content_x_px = 0.0;
  double content_y_px = 0.0;
  double content_width_px = 0.0;
  double content_height_px = 0.0;
};

// Fill spacing / safe-area fields from a Style Bible when tokens are present.
[[nodiscard]] EditorialAsymRequest editorial_asym_request_from_style(
    const StyleBible &bible, std::uint32_t element_count,
    double canvas_width_px = 1080.0, double canvas_height_px = 1920.0);

// Build the asymmetrical stack and compute the importance-weighted focal point.
[[nodiscard]] std::expected<EditorialAsymLayout, diagnostics::Error>
compose_editorial_asymmetrical(const EditorialAsymRequest &request);

// True when the focal point lies inside the readable band (acceptance check).
[[nodiscard]] bool
editorial_focal_is_readable(const EditorialFocalPoint &focal) noexcept;

[[nodiscard]] inline constexpr std::string_view
editorial_asym_slot_id(std::uint32_t index) noexcept {
  switch (index) {
  case 0:
    return "headline";
  case 1:
    return "accent_bar";
  case 2:
    return "body";
  case 3:
    return "callout";
  case 4:
    return "photo";
  default:
    return "slot";
  }
}

} // namespace instantee::direction
