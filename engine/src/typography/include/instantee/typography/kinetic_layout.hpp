#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace instantee::typography {

// Typed kinetic typography levels (SAS §typography). Codepoint slicing is not a
// substitute: units are built from ICU breaks + HarfBuzz clusters after shape.
enum class KineticLevel : std::uint8_t {
  block = 1,
  line,
  word,
  grapheme,
  glyph,
};

struct KineticUnit {
  KineticLevel level = KineticLevel::glyph;
  // Index among units at the same level (stable for stagger presets).
  std::uint32_t index = 0;
  // UTF-8 byte range into the original input (half-open).
  std::uint32_t utf8_begin = 0;
  std::uint32_t utf8_end = 0;
  // Origin/width of the unit's glyph run in 26.6 fixed-point pixels.
  std::int32_t origin_x_26_6 = 0;
  std::int32_t origin_y_26_6 = 0;
  std::int32_t width_26_6 = 0;
  // Indices into ShapedText::glyphs. Ligatures and mark clusters stay together.
  std::vector<std::uint32_t> glyph_indices{};
};

struct KineticLayout {
  KineticUnit block{};
  std::vector<KineticUnit> lines{};
  std::vector<KineticUnit> words{};
  std::vector<KineticUnit> graphemes{};
  // One unit per shaped glyph; utf8 range follows the HarfBuzz cluster span.
  std::vector<KineticUnit> glyphs{};
  // Cumulative pen positions for each shaped glyph before kinetic offsets.
  std::vector<std::int32_t> glyph_origin_x_26_6{};
  std::vector<std::int32_t> glyph_origin_y_26_6{};
};

[[nodiscard]] constexpr std::string_view
kinetic_level_id(KineticLevel level) noexcept {
  switch (level) {
  case KineticLevel::block:
    return "block";
  case KineticLevel::line:
    return "line";
  case KineticLevel::word:
    return "word";
  case KineticLevel::grapheme:
    return "grapheme";
  case KineticLevel::glyph:
    return "glyph";
  }
  return "unknown";
}

// Build the kinetic hierarchy from already-shaped text. Does not reshape; glyph
// identities and font_index values are preserved for animation sampling.
[[nodiscard]] std::expected<KineticLayout, diagnostics::Error>
build_kinetic_layout(std::string_view utf8, const ShapedText &shaped);

[[nodiscard]] std::span<const KineticUnit>
kinetic_units_at(const KineticLayout &layout, KineticLevel level) noexcept;

// Grapheme units that share one or more shaped glyphs (e.g. "fi" ligature) are
// merged so per-grapheme animation cannot tear a shaping cluster apart.
[[nodiscard]] std::vector<std::vector<std::uint32_t>>
kinetic_cluster_locks(const KineticLayout &layout);

// Apply per-unit X translations at `level` without reshaping. `unit_dx_26_6`
// must match kinetic_units_at(layout, level).size(). Locked grapheme groups
// (when level == grapheme) receive the first unit's dx for every member.
[[nodiscard]] std::expected<std::vector<std::int32_t>, diagnostics::Error>
sample_kinetic_origins_x(const KineticLayout &layout, KineticLevel level,
                         std::span<const std::int32_t> unit_dx_26_6);

} // namespace instantee::typography
