#pragma once

#include <instantee/asset_pipeline/texture_atlas.hpp>
#include <instantee/diagnostics/error.hpp>
#include <instantee/typography/text_shape.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace instantee::typography {

// Render identity folded into cache keys (hinting / LCD / etc. later).
struct GlyphRenderFlags {
  std::uint32_t bits = 0;
};

struct GlyphAtlasConfig {
  asset_pipeline::TextureAtlasConfig atlas{};
  // Soft cap on cached glyph bitmaps. Eviction runs only for glyphs not pinned
  // by any in-flight frame.
  std::uint32_t max_cached_glyphs = 2048;
  std::uint32_t max_cached_layouts = 256;
  std::uint32_t max_in_flight_frames = 4;
};

struct GlyphAtlasStats {
  std::uint64_t layout_hits = 0;
  std::uint64_t layout_misses = 0;
  std::uint64_t glyph_hits = 0;
  std::uint64_t glyph_misses = 0;
  std::uint64_t shape_calls = 0;
  std::uint64_t raster_calls = 0;
  std::uint64_t frames_begun = 0;
  std::uint64_t frames_ended = 0;
  std::uint64_t evictions = 0;
  std::uint64_t compact_rebuilds = 0;
};

struct CachedGlyphQuad {
  std::uint32_t glyph_id = 0;
  std::uint32_t font_index = 0;
  std::uint32_t cluster = 0;
  std::int32_t x_advance_26_6 = 0;
  std::int32_t y_advance_26_6 = 0;
  std::int32_t x_offset_26_6 = 0;
  std::int32_t y_offset_26_6 = 0;
  std::int32_t bitmap_left = 0;
  std::int32_t bitmap_top = 0;
  asset_pipeline::AtlasRect atlas_rect{};
};

struct CachedGlyphRun {
  std::vector<CachedGlyphQuad> glyphs{};
  std::int32_t total_x_advance_26_6 = 0;
  std::uint32_t grapheme_count = 0;
  bool from_layout_cache = false;
};

// CPU glyph atlas over TextShaper + TextureAtlas. Caches layouts and raster
// bitmaps by font fingerprint / size / flags. Eviction is deferred until no
// in-flight frame still references the entry. Atlas packing is rebuilt after
// eviction so UV rects stay coherent with remaining glyphs.
class GlyphAtlas {
public:
  explicit GlyphAtlas(GlyphAtlasConfig config = {});

  [[nodiscard]] const GlyphAtlasConfig &config() const noexcept {
    return config_;
  }
  [[nodiscard]] const GlyphAtlasStats &stats() const noexcept { return stats_; }
  [[nodiscard]] asset_pipeline::TextureAtlas &cpu_atlas() noexcept {
    return atlas_;
  }
  [[nodiscard]] const asset_pipeline::TextureAtlas &cpu_atlas() const noexcept {
    return atlas_;
  }
  // Bumps when packing is rebuilt so GPU page uploads can be invalidated.
  [[nodiscard]] std::uint64_t atlas_generation() const noexcept {
    return atlas_generation_;
  }

  // Opens a pin scope for `frame_id`. Nested duplicate ids are rejected.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  begin_frame(std::uint64_t frame_id);

  // Drops the pin and may evict LRU glyphs/layouts not referenced by remaining
  // in-flight frames.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  end_frame(std::uint64_t frame_id);

  // Shape+raster with caching. `font_fingerprint` is an explicit identity
  // (path hash / content hash) — never an implicit system font name.
  [[nodiscard]] std::expected<CachedGlyphRun, diagnostics::Error>
  prepare(TextShaper &shaper, FontStack &fonts,
          std::string_view font_fingerprint, std::string_view utf8,
          float size_px, GlyphRenderFlags flags = {});

private:
  struct GlyphEntry {
    std::string key{};
    asset_pipeline::AtlasRect rect{};
    std::int32_t bitmap_left = 0;
    std::int32_t bitmap_top = 0;
    std::uint32_t width_px = 0;
    std::uint32_t height_px = 0;
    std::list<std::string>::iterator lru{};
  };

  // Layout cache stores placements only; atlas rects are resolved from live
  // GlyphEntry state so compact rebuilds cannot stale UV coordinates.
  struct LayoutPlacement {
    std::uint32_t glyph_id = 0;
    std::uint32_t font_index = 0;
    std::uint32_t cluster = 0;
    std::int32_t x_advance_26_6 = 0;
    std::int32_t y_advance_26_6 = 0;
    std::int32_t x_offset_26_6 = 0;
    std::int32_t y_offset_26_6 = 0;
  };

  struct LayoutEntry {
    std::string key{};
    std::vector<LayoutPlacement> placements{};
    std::int32_t total_x_advance_26_6 = 0;
    std::uint32_t grapheme_count = 0;
    std::list<std::string>::iterator lru{};
  };

  struct FramePin {
    std::uint64_t frame_id = 0;
    std::unordered_set<std::string> glyph_keys{};
    std::unordered_set<std::string> layout_keys{};
  };

  [[nodiscard]] static std::string
  make_glyph_key(std::string_view font_fingerprint, std::uint32_t font_index,
                 std::uint32_t glyph_id, float size_px, GlyphRenderFlags flags);

  [[nodiscard]] static std::string
  make_layout_key(std::string_view font_fingerprint, std::string_view utf8,
                  float size_px, GlyphRenderFlags flags);

  [[nodiscard]] static std::uint32_t size_key(float size_px);

  [[nodiscard]] bool glyph_pinned(const std::string &key) const;
  [[nodiscard]] bool layout_pinned(const std::string &key) const;

  void touch_glyph(const std::string &key);
  void touch_layout(const std::string &key);
  void note_frame_use(const std::string &glyph_key,
                      const std::string &layout_key);

  void evict_if_needed();
  [[nodiscard]] std::expected<void, diagnostics::Error>
  ensure_glyph(TextShaper &shaper, FontStack &fonts,
               std::string_view font_fingerprint,
               const GlyphPlacement &placement, float size_px,
               GlyphRenderFlags flags, GlyphEntry *&out);

  [[nodiscard]] std::expected<void, diagnostics::Error> rebuild_atlas_pages();

  [[nodiscard]] std::expected<CachedGlyphRun, diagnostics::Error>
  materialize_run(const LayoutEntry &layout, std::string_view font_fingerprint,
                  float size_px, GlyphRenderFlags flags, bool from_cache);

  GlyphAtlasConfig config_{};
  GlyphAtlasStats stats_{};
  asset_pipeline::TextureAtlas atlas_;
  std::uint64_t atlas_generation_ = 1;
  std::unordered_map<std::string, GlyphEntry> glyphs_{};
  std::unordered_map<std::string, LayoutEntry> layouts_{};
  std::list<std::string> glyph_lru_{};
  std::list<std::string> layout_lru_{};
  std::vector<FramePin> in_flight_{};
  // Gray→RGBA bitmaps retained so compact can rebuild atlas packing.
  std::unordered_map<std::string, std::vector<std::uint8_t>> glyph_rgba_{};
};

} // namespace instantee::typography
