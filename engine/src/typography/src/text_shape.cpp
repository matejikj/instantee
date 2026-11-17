#include <instantee/typography/text_shape.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace instantee::typography {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "typography", message};
}

[[nodiscard]] diagnostics::Error make_missing(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::missing_or_corrupt_asset,
                            diagnostics::ErrorKind::missing_or_corrupt_asset,
                            code, "typography", message};
}

struct FtLibraryDeleter {
  void operator()(FT_LibraryRec_ *lib) const noexcept {
    if (lib != nullptr) {
      FT_Done_FreeType(lib);
    }
  }
};

struct FtFaceDeleter {
  void operator()(FT_FaceRec_ *face) const noexcept {
    if (face != nullptr) {
      FT_Done_Face(face);
    }
  }
};

struct HbFontDeleter {
  void operator()(hb_font_t *font) const noexcept {
    if (font != nullptr) {
      hb_font_destroy(font);
    }
  }
};

struct HbBufferDeleter {
  void operator()(hb_buffer_t *buf) const noexcept {
    if (buf != nullptr) {
      hb_buffer_destroy(buf);
    }
  }
};

using FtLibraryPtr = std::unique_ptr<FT_LibraryRec_, FtLibraryDeleter>;
using FtFacePtr = std::unique_ptr<FT_FaceRec_, FtFaceDeleter>;
using HbFontPtr = std::unique_ptr<hb_font_t, HbFontDeleter>;
using HbBufferPtr = std::unique_ptr<hb_buffer_t, HbBufferDeleter>;

struct LoadedFace {
  FtFacePtr face{};
  HbFontPtr hb_font{};
  std::filesystem::path path{};
};

[[nodiscard]] std::expected<void, diagnostics::Error>
set_pixel_size(FT_Face face, float size_px) {
  if (!(size_px > 0.0f) || !std::isfinite(size_px) || size_px > 1024.0f) {
    return std::unexpected(
        make_invalid("font_size", "font size_px must be in (0, 1024]"));
  }
  const FT_UInt px = static_cast<FT_UInt>(std::lround(size_px));
  if (FT_Set_Pixel_Sizes(face, 0, px) != 0) {
    return std::unexpected(
        make_invalid("font_size_set_failed", "FT_Set_Pixel_Sizes failed"));
  }
  return {};
}

[[nodiscard]] std::uint32_t count_graphemes(std::string_view utf8) {
  if (utf8.empty()) {
    return 0;
  }
  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator *it = ubrk_open(UBRK_CHARACTER, "cs", nullptr, 0, &status);
  if (U_FAILURE(status) || it == nullptr) {
    return 0;
  }
  // ICU break iterators want UChar; convert UTF-8 → UTF-16.
  status = U_ZERO_ERROR;
  const int32_t capacity =
      static_cast<int32_t>(utf8.size() + 1); // upper bound for BMP-heavy text
  std::vector<UChar> utf16(static_cast<std::size_t>(capacity));
  int32_t utf16_len = 0;
  u_strFromUTF8(utf16.data(), capacity, &utf16_len, utf8.data(),
                static_cast<int32_t>(utf8.size()), &status);
  if (U_FAILURE(status)) {
    ubrk_close(it);
    return 0;
  }
  status = U_ZERO_ERROR;
  ubrk_setText(it, utf16.data(), utf16_len, &status);
  if (U_FAILURE(status)) {
    ubrk_close(it);
    return 0;
  }
  std::uint32_t count = 0;
  ubrk_first(it);
  for (;;) {
    const int32_t end = ubrk_next(it);
    if (end == UBRK_DONE) {
      break;
    }
    ++count;
  }
  ubrk_close(it);
  return count;
}

[[nodiscard]] std::int8_t paragraph_bidi_level(std::string_view utf8) {
  if (utf8.empty()) {
    return 0;
  }
  UErrorCode status = U_ZERO_ERROR;
  const int32_t capacity = static_cast<int32_t>(utf8.size() + 1);
  std::vector<UChar> utf16(static_cast<std::size_t>(capacity));
  int32_t utf16_len = 0;
  u_strFromUTF8(utf16.data(), capacity, &utf16_len, utf8.data(),
                static_cast<int32_t>(utf8.size()), &status);
  if (U_FAILURE(status)) {
    return 0;
  }
  UBiDi *bidi = ubidi_openSized(utf16_len, 0, &status);
  if (U_FAILURE(status) || bidi == nullptr) {
    return 0;
  }
  ubidi_setPara(bidi, utf16.data(), utf16_len, UBIDI_DEFAULT_LTR, nullptr,
                &status);
  std::int8_t level = 0;
  if (U_SUCCESS(status)) {
    level = static_cast<std::int8_t>(ubidi_getParaLevel(bidi));
  }
  ubidi_close(bidi);
  return level;
}

[[nodiscard]] std::expected<std::vector<GlyphPlacement>, diagnostics::Error>
shape_with_font(hb_font_t *hb_font, std::string_view utf8,
                std::uint32_t font_index) {
  HbBufferPtr buffer(hb_buffer_create());
  if (!buffer) {
    return std::unexpected(
        make_invalid("hb_buffer", "hb_buffer_create failed"));
  }
  hb_buffer_add_utf8(buffer.get(), utf8.data(), static_cast<int>(utf8.size()),
                     0, static_cast<int>(utf8.size()));
  hb_buffer_guess_segment_properties(buffer.get());

  const hb_feature_t features[] = {
      {HB_TAG('l', 'i', 'g', 'a'), 1, 0, static_cast<unsigned>(-1)},
      {HB_TAG('c', 'l', 'i', 'g'), 1, 0, static_cast<unsigned>(-1)},
  };
  hb_shape(hb_font, buffer.get(), features, 2);

  unsigned glyph_count = 0;
  hb_glyph_info_t *infos =
      hb_buffer_get_glyph_infos(buffer.get(), &glyph_count);
  hb_glyph_position_t *pos =
      hb_buffer_get_glyph_positions(buffer.get(), &glyph_count);
  if (infos == nullptr || pos == nullptr) {
    return std::unexpected(
        make_invalid("hb_shape", "harfbuzz returned null glyph arrays"));
  }

  std::vector<GlyphPlacement> out;
  out.reserve(glyph_count);
  for (unsigned i = 0; i < glyph_count; ++i) {
    GlyphPlacement g;
    g.glyph_id = infos[i].codepoint;
    g.cluster = infos[i].cluster;
    g.x_advance_26_6 = pos[i].x_advance;
    g.y_advance_26_6 = pos[i].y_advance;
    g.x_offset_26_6 = pos[i].x_offset;
    g.y_offset_26_6 = pos[i].y_offset;
    g.font_index = font_index;
    out.push_back(g);
  }
  return out;
}

} // namespace

struct FontStack::Impl {
  FtLibraryPtr library{};
  std::vector<LoadedFace> faces{};
};

FontStack::FontStack() : impl_(std::make_unique<Impl>()) {}
FontStack::FontStack(FontStack &&) noexcept = default;
FontStack &FontStack::operator=(FontStack &&) noexcept = default;
FontStack::~FontStack() = default;

std::size_t FontStack::size() const noexcept {
  return impl_ ? impl_->faces.size() : 0;
}

std::expected<FontStack, diagnostics::Error>
FontStack::open(std::span<const std::filesystem::path> font_files) {
  if (font_files.empty()) {
    return std::unexpected(make_invalid(
        "font_stack_empty", "font stack requires at least one face"));
  }
  FontStack stack;
  FT_Library raw_lib = nullptr;
  if (FT_Init_FreeType(&raw_lib) != 0 || raw_lib == nullptr) {
    return std::unexpected(
        make_invalid("freetype_init", "FT_Init_FreeType failed"));
  }
  stack.impl_->library.reset(raw_lib);

  for (const auto &path : font_files) {
    if (path.empty()) {
      return std::unexpected(
          make_invalid("font_path_empty", "font path must be non-empty"));
    }
    FT_Face raw_face = nullptr;
    if (FT_New_Face(stack.impl_->library.get(), path.string().c_str(), 0,
                    &raw_face) != 0 ||
        raw_face == nullptr) {
      return std::unexpected(make_missing(
          "font_open_failed", "FT_New_Face failed for a pinned font path"));
    }
    LoadedFace loaded;
    loaded.face.reset(raw_face);
    loaded.path = path;
    hb_font_t *hb = hb_ft_font_create_referenced(raw_face);
    if (hb == nullptr) {
      return std::unexpected(
          make_invalid("hb_ft_font", "hb_ft_font_create_referenced failed"));
    }
    loaded.hb_font.reset(hb);
    stack.impl_->faces.push_back(std::move(loaded));
  }
  return stack;
}

struct TextShaper::Impl {};

TextShaper::TextShaper() : impl_(std::make_unique<Impl>()) {}
TextShaper::TextShaper(TextShaper &&) noexcept = default;
TextShaper &TextShaper::operator=(TextShaper &&) noexcept = default;
TextShaper::~TextShaper() = default;

std::expected<ShapedText, diagnostics::Error>
TextShaper::shape(FontStack &fonts, std::string_view utf8,
                  float size_px) const {
  if (!fonts.impl_ || fonts.impl_->faces.empty()) {
    return std::unexpected(
        make_invalid("font_stack_empty", "font stack has no faces"));
  }
  if (utf8.empty()) {
    ShapedText empty;
    empty.grapheme_count = 0;
    empty.paragraph_level = 0;
    return empty;
  }
  // Reject embedded NUL — cluster math assumes a contiguous view.
  if (utf8.find('\0') != std::string_view::npos) {
    return std::unexpected(
        make_invalid("text_embedded_nul", "UTF-8 text must not contain NUL"));
  }

  for (LoadedFace &face : fonts.impl_->faces) {
    auto sized = set_pixel_size(face.face.get(), size_px);
    if (!sized) {
      return std::unexpected(sized.error());
    }
    hb_ft_font_changed(face.hb_font.get());
  }

  // Primary shape.
  auto primary = shape_with_font(fonts.impl_->faces[0].hb_font.get(), utf8, 0);
  if (!primary) {
    return std::unexpected(primary.error());
  }

  std::vector<GlyphPlacement> merged;
  merged.reserve(primary->size());

  std::size_t i = 0;
  while (i < primary->size()) {
    if ((*primary)[i].glyph_id != 0) {
      merged.push_back((*primary)[i]);
      ++i;
      continue;
    }
    // Contiguous .notdef run from primary — reshape substring with fallbacks.
    const std::uint32_t cluster_begin = (*primary)[i].cluster;
    std::size_t j = i;
    while (j < primary->size() && (*primary)[j].glyph_id == 0) {
      ++j;
    }
    std::uint32_t cluster_end = static_cast<std::uint32_t>(utf8.size());
    if (j < primary->size()) {
      cluster_end = (*primary)[j].cluster;
    }
    if (cluster_end < cluster_begin ||
        cluster_end > static_cast<std::uint32_t>(utf8.size())) {
      return std::unexpected(make_invalid(
          "cluster_range", "harfbuzz cluster range is inconsistent"));
    }
    const std::string_view slice =
        utf8.substr(cluster_begin, cluster_end - cluster_begin);

    bool resolved = false;
    for (std::uint32_t fi = 1; fi < fonts.impl_->faces.size(); ++fi) {
      auto alt =
          shape_with_font(fonts.impl_->faces[fi].hb_font.get(), slice, fi);
      if (!alt) {
        return std::unexpected(alt.error());
      }
      bool ok = !alt->empty();
      for (auto &g : *alt) {
        if (g.glyph_id == 0) {
          ok = false;
          break;
        }
        g.cluster += cluster_begin;
      }
      if (ok) {
        merged.insert(merged.end(), alt->begin(), alt->end());
        resolved = true;
        break;
      }
    }
    if (!resolved) {
      return std::unexpected(
          make_missing("glyph_missing",
                       "no font in the stack covers one or more codepoints"));
    }
    i = j;
  }

  ShapedText out;
  out.glyphs = std::move(merged);
  for (const auto &g : out.glyphs) {
    out.total_x_advance_26_6 += g.x_advance_26_6;
  }
  out.grapheme_count = count_graphemes(utf8);
  out.paragraph_level = paragraph_bidi_level(utf8);
  return out;
}

std::expected<GlyphBitmap, diagnostics::Error>
TextShaper::rasterize(FontStack &fonts, std::uint32_t font_index,
                      std::uint32_t glyph_id, float size_px) const {
  if (!fonts.impl_ || font_index >= fonts.impl_->faces.size()) {
    return std::unexpected(
        make_invalid("font_index", "font_index is out of range"));
  }
  LoadedFace &face = fonts.impl_->faces[font_index];
  auto sized = set_pixel_size(face.face.get(), size_px);
  if (!sized) {
    return std::unexpected(sized.error());
  }
  if (FT_Load_Glyph(face.face.get(), glyph_id, FT_LOAD_DEFAULT) != 0) {
    return std::unexpected(make_invalid("glyph_load", "FT_Load_Glyph failed"));
  }
  if (FT_Render_Glyph(face.face.get()->glyph, FT_RENDER_MODE_NORMAL) != 0) {
    return std::unexpected(
        make_invalid("glyph_render", "FT_Render_Glyph failed"));
  }
  const FT_Bitmap &bm = face.face.get()->glyph->bitmap;
  GlyphBitmap out;
  out.width_px = bm.width;
  out.height_px = bm.rows;
  out.bitmap_left = face.face.get()->glyph->bitmap_left;
  out.bitmap_top = face.face.get()->glyph->bitmap_top;
  out.gray.resize(static_cast<std::size_t>(bm.width) * bm.rows);
  if (bm.width == 0 || bm.rows == 0) {
    return out;
  }
  for (unsigned row = 0; row < bm.rows; ++row) {
    const unsigned char *src =
        bm.buffer + static_cast<std::size_t>(row) * bm.pitch;
    std::uint8_t *dst =
        out.gray.data() + static_cast<std::size_t>(row) * bm.width;
    if (bm.pixel_mode == FT_PIXEL_MODE_GRAY) {
      std::memcpy(dst, src, bm.width);
    } else {
      return std::unexpected(make_invalid(
          "glyph_pixel_mode", "unsupported FreeType bitmap pixel mode"));
    }
  }
  return out;
}

} // namespace instantee::typography
