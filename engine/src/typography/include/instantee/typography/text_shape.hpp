#pragma once

#include <instantee/diagnostics/error.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace instantee::typography {

struct GlyphPlacement {
  std::uint32_t glyph_id = 0;
  // Byte offset into the original UTF-8 input (HarfBuzz cluster).
  std::uint32_t cluster = 0;
  // Advances/offsets in 26.6 fixed-point pixels (divide by 64 for floats).
  std::int32_t x_advance_26_6 = 0;
  std::int32_t y_advance_26_6 = 0;
  std::int32_t x_offset_26_6 = 0;
  std::int32_t y_offset_26_6 = 0;
  // Index into the FontStack used for this glyph (0 = primary).
  std::uint32_t font_index = 0;
};

struct ShapedText {
  std::vector<GlyphPlacement> glyphs{};
  std::int32_t total_x_advance_26_6 = 0;
  std::uint32_t grapheme_count = 0; // ICU UBRK_CHARACTER count
  std::int8_t paragraph_level = 0;  // ICU ubidi base level (0 = LTR)
};

struct GlyphBitmap {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::int32_t bitmap_left = 0;
  std::int32_t bitmap_top = 0;
  // Tight gray coverage, row-major, size == width*height.
  std::vector<std::uint8_t> gray{};
};

// Ordered font stack. Index 0 is primary; later faces cover missing glyphs.
class FontStack {
public:
  FontStack();
  FontStack(FontStack &&) noexcept;
  FontStack &operator=(FontStack &&) noexcept;
  ~FontStack();

  FontStack(const FontStack &) = delete;
  FontStack &operator=(const FontStack &) = delete;

  [[nodiscard]] static std::expected<FontStack, diagnostics::Error>
  open(std::span<const std::filesystem::path> font_files);

  [[nodiscard]] std::size_t size() const noexcept;

private:
  friend class TextShaper;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TextShaper {
public:
  TextShaper();
  TextShaper(TextShaper &&) noexcept;
  TextShaper &operator=(TextShaper &&) noexcept;
  ~TextShaper();

  TextShaper(const TextShaper &) = delete;
  TextShaper &operator=(const TextShaper &) = delete;

  // Shape UTF-8 text at `size_px` using the font stack. Enables liga/clig.
  // Missing glyphs fall back through the stack; unresolved .notdef fails.
  // Mutates FreeType pixel sizes on the stack faces for this call.
  [[nodiscard]] std::expected<ShapedText, diagnostics::Error>
  shape(FontStack &fonts, std::string_view utf8, float size_px) const;

  // Rasterize one glyph from the stack face that owns it. Coverage only.
  [[nodiscard]] std::expected<GlyphBitmap, diagnostics::Error>
  rasterize(FontStack &fonts, std::uint32_t font_index, std::uint32_t glyph_id,
            float size_px) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace instantee::typography
