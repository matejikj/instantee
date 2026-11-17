#include <instantee/typography/glyph_atlas.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace instantee::typography {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "typography.glyph_atlas", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "typography.glyph_atlas", message};
}

[[nodiscard]] diagnostics::Error make_missing(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::missing_or_corrupt_asset,
                            diagnostics::ErrorKind::missing_or_corrupt_asset,
                            code, "typography.glyph_atlas", message};
}

} // namespace

GlyphAtlas::GlyphAtlas(GlyphAtlasConfig config)
    : config_(std::move(config)), atlas_(config_.atlas) {
  if (config_.max_cached_glyphs == 0) {
    config_.max_cached_glyphs = 1;
  }
  if (config_.max_cached_layouts == 0) {
    config_.max_cached_layouts = 1;
  }
  if (config_.max_in_flight_frames == 0) {
    config_.max_in_flight_frames = 1;
  }
}

std::uint32_t GlyphAtlas::size_key(float size_px) {
  if (!(size_px > 0.0f) || !std::isfinite(size_px)) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::lround(size_px * 64.0f));
}

std::string GlyphAtlas::make_glyph_key(std::string_view font_fingerprint,
                                       std::uint32_t font_index,
                                       std::uint32_t glyph_id, float size_px,
                                       GlyphRenderFlags flags) {
  return std::format("g|{}|{}|{}|{}|{}", font_fingerprint, font_index, glyph_id,
                     size_key(size_px), flags.bits);
}

std::string GlyphAtlas::make_layout_key(std::string_view font_fingerprint,
                                        std::string_view utf8, float size_px,
                                        GlyphRenderFlags flags) {
  return std::format("l|{}|{}|{}|{}", font_fingerprint, size_key(size_px),
                     flags.bits, utf8);
}

std::expected<void, diagnostics::Error>
GlyphAtlas::begin_frame(std::uint64_t frame_id) {
  for (const auto &pin : in_flight_) {
    if (pin.frame_id == frame_id) {
      return std::unexpected(
          make_invalid("glyph_atlas_frame_inflight",
                       std::format("frame already in flight: {}", frame_id)));
    }
  }
  if (in_flight_.size() >= config_.max_in_flight_frames) {
    return std::unexpected(
        make_budget("glyph_atlas_inflight_limit",
                    std::format("too many in-flight frames (limit={})",
                                config_.max_in_flight_frames)));
  }
  in_flight_.push_back(FramePin{.frame_id = frame_id});
  ++stats_.frames_begun;
  return {};
}

std::expected<void, diagnostics::Error>
GlyphAtlas::end_frame(std::uint64_t frame_id) {
  const auto it = std::find_if(
      in_flight_.begin(), in_flight_.end(),
      [frame_id](const FramePin &p) { return p.frame_id == frame_id; });
  if (it == in_flight_.end()) {
    return std::unexpected(
        make_missing("glyph_atlas_frame_missing",
                     std::format("frame not in flight: {}", frame_id)));
  }
  in_flight_.erase(it);
  ++stats_.frames_ended;
  evict_if_needed();
  return {};
}

bool GlyphAtlas::glyph_pinned(const std::string &key) const {
  for (const auto &pin : in_flight_) {
    if (pin.glyph_keys.contains(key)) {
      return true;
    }
  }
  return false;
}

bool GlyphAtlas::layout_pinned(const std::string &key) const {
  for (const auto &pin : in_flight_) {
    if (pin.layout_keys.contains(key)) {
      return true;
    }
  }
  return false;
}

void GlyphAtlas::touch_glyph(const std::string &key) {
  auto it = glyphs_.find(key);
  if (it == glyphs_.end()) {
    return;
  }
  glyph_lru_.erase(it->second.lru);
  glyph_lru_.push_front(key);
  it->second.lru = glyph_lru_.begin();
}

void GlyphAtlas::touch_layout(const std::string &key) {
  auto it = layouts_.find(key);
  if (it == layouts_.end()) {
    return;
  }
  layout_lru_.erase(it->second.lru);
  layout_lru_.push_front(key);
  it->second.lru = layout_lru_.begin();
}

void GlyphAtlas::note_frame_use(const std::string &glyph_key,
                                const std::string &layout_key) {
  if (in_flight_.empty()) {
    return;
  }
  auto &pin = in_flight_.back();
  if (!glyph_key.empty()) {
    pin.glyph_keys.insert(glyph_key);
  }
  if (!layout_key.empty()) {
    pin.layout_keys.insert(layout_key);
  }
}

std::expected<void, diagnostics::Error> GlyphAtlas::rebuild_atlas_pages() {
  asset_pipeline::TextureAtlas rebuilt(config_.atlas);
  for (auto &entry : glyphs_) {
    auto rgba_it = glyph_rgba_.find(entry.first);
    if (rgba_it == glyph_rgba_.end()) {
      return std::unexpected(make_missing(
          "glyph_atlas_rgba_missing",
          std::format("missing glyph rgba during rebuild: {}", entry.first)));
    }
    auto packed = rebuilt.insert(entry.first, entry.second.width_px,
                                 entry.second.height_px, rgba_it->second);
    if (!packed) {
      return std::unexpected(packed.error());
    }
    entry.second.rect = (*packed)->rect;
  }
  atlas_ = std::move(rebuilt);
  ++atlas_generation_;
  ++stats_.compact_rebuilds;
  return {};
}

void GlyphAtlas::evict_if_needed() {
  bool removed_any = false;
  while (glyphs_.size() > config_.max_cached_glyphs) {
    bool removed = false;
    for (auto it = glyph_lru_.rbegin(); it != glyph_lru_.rend(); ++it) {
      if (glyph_pinned(*it)) {
        continue;
      }
      const std::string key = *it;
      glyphs_.erase(key);
      glyph_rgba_.erase(key);
      glyph_lru_.erase(std::next(it).base());
      ++stats_.evictions;
      removed = true;
      removed_any = true;
      break;
    }
    if (!removed) {
      break;
    }
  }

  while (layouts_.size() > config_.max_cached_layouts) {
    bool removed = false;
    for (auto it = layout_lru_.rbegin(); it != layout_lru_.rend(); ++it) {
      if (layout_pinned(*it)) {
        continue;
      }
      const std::string key = *it;
      layouts_.erase(key);
      layout_lru_.erase(std::next(it).base());
      ++stats_.evictions;
      removed = true;
      break;
    }
    if (!removed) {
      break;
    }
  }

  if (removed_any) {
    (void)rebuild_atlas_pages();
  }
}

std::expected<void, diagnostics::Error>
GlyphAtlas::ensure_glyph(TextShaper &shaper, FontStack &fonts,
                         std::string_view font_fingerprint,
                         const GlyphPlacement &placement, float size_px,
                         GlyphRenderFlags flags, GlyphEntry *&out) {
  const auto key = make_glyph_key(font_fingerprint, placement.font_index,
                                  placement.glyph_id, size_px, flags);
  if (auto it = glyphs_.find(key); it != glyphs_.end()) {
    ++stats_.glyph_hits;
    touch_glyph(key);
    note_frame_use(key, {});
    out = &it->second;
    return {};
  }

  ++stats_.glyph_misses;
  ++stats_.raster_calls;
  auto raster = shaper.rasterize(fonts, placement.font_index,
                                 placement.glyph_id, size_px);
  if (!raster) {
    return std::unexpected(raster.error());
  }

  // Empty coverage still occupies a 1x1 transparent slot so advances stay
  // addressable without special-casing missing bitmaps in the GPU path.
  std::uint32_t width = raster->width_px;
  std::uint32_t height = raster->height_px;
  if (width == 0 || height == 0) {
    width = 1;
    height = 1;
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) *
                                     static_cast<std::size_t>(height) * 4u,
                                 0);
  if (raster->width_px > 0 && raster->height_px > 0) {
    for (std::uint32_t y = 0; y < raster->height_px; ++y) {
      for (std::uint32_t x = 0; x < raster->width_px; ++x) {
        const std::size_t src =
            static_cast<std::size_t>(y) * raster->width_px + x;
        const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4u;
        const std::uint8_t a = raster->gray[src];
        rgba[dst + 0] = 255;
        rgba[dst + 1] = 255;
        rgba[dst + 2] = 255;
        rgba[dst + 3] = a;
      }
    }
  }

  auto packed = atlas_.insert(key, width, height, rgba);
  if (!packed) {
    // Drop unpinned entries, compact, then retry once.
    evict_if_needed();
    packed = atlas_.insert(key, width, height, rgba);
    if (!packed) {
      return std::unexpected(packed.error());
    }
  }

  GlyphEntry entry{};
  entry.key = key;
  entry.rect = (*packed)->rect;
  entry.bitmap_left = raster->bitmap_left;
  entry.bitmap_top = raster->bitmap_top;
  entry.width_px = width;
  entry.height_px = height;
  glyph_lru_.push_front(key);
  entry.lru = glyph_lru_.begin();
  glyph_rgba_[key] = std::move(rgba);
  auto [ins, ok] = glyphs_.emplace(key, std::move(entry));
  if (!ok) {
    return std::unexpected(make_invalid("glyph_atlas_insert_collision", key));
  }
  note_frame_use(key, {});
  out = &ins->second;
  // Eviction is deferred to end_frame so a mid-prepare insert cannot be
  // dropped before the caller materializes the run.
  return {};
}

std::expected<CachedGlyphRun, diagnostics::Error>
GlyphAtlas::materialize_run(const LayoutEntry &layout,
                            std::string_view font_fingerprint, float size_px,
                            GlyphRenderFlags flags, bool from_cache) {
  CachedGlyphRun run{};
  run.total_x_advance_26_6 = layout.total_x_advance_26_6;
  run.grapheme_count = layout.grapheme_count;
  run.from_layout_cache = from_cache;
  run.glyphs.reserve(layout.placements.size());

  for (const auto &placement : layout.placements) {
    const auto gkey = make_glyph_key(font_fingerprint, placement.font_index,
                                     placement.glyph_id, size_px, flags);
    auto git = glyphs_.find(gkey);
    if (git == glyphs_.end()) {
      return std::unexpected(make_missing(
          "glyph_atlas_layout_orphan",
          std::format("layout references missing glyph: {}", gkey)));
    }
    touch_glyph(gkey);
    note_frame_use(gkey, {});
    CachedGlyphQuad quad{};
    quad.glyph_id = placement.glyph_id;
    quad.font_index = placement.font_index;
    quad.cluster = placement.cluster;
    quad.x_advance_26_6 = placement.x_advance_26_6;
    quad.y_advance_26_6 = placement.y_advance_26_6;
    quad.x_offset_26_6 = placement.x_offset_26_6;
    quad.y_offset_26_6 = placement.y_offset_26_6;
    quad.bitmap_left = git->second.bitmap_left;
    quad.bitmap_top = git->second.bitmap_top;
    quad.atlas_rect = git->second.rect;
    run.glyphs.push_back(quad);
  }
  return run;
}

std::expected<CachedGlyphRun, diagnostics::Error>
GlyphAtlas::prepare(TextShaper &shaper, FontStack &fonts,
                    std::string_view font_fingerprint, std::string_view utf8,
                    float size_px, GlyphRenderFlags flags) {
  if (font_fingerprint.empty()) {
    return std::unexpected(make_invalid("glyph_atlas_empty_fingerprint",
                                        "font_fingerprint must be non-empty"));
  }
  if (!(size_px > 0.0f) || !std::isfinite(size_px)) {
    return std::unexpected(
        make_invalid("glyph_atlas_bad_size", "size_px must be finite and > 0"));
  }

  const auto layout_key =
      make_layout_key(font_fingerprint, utf8, size_px, flags);
  if (auto it = layouts_.find(layout_key); it != layouts_.end()) {
    ++stats_.layout_hits;
    touch_layout(layout_key);
    note_frame_use({}, layout_key);
    return materialize_run(it->second, font_fingerprint, size_px, flags, true);
  }

  ++stats_.layout_misses;
  ++stats_.shape_calls;
  auto shaped = shaper.shape(fonts, utf8, size_px);
  if (!shaped) {
    return std::unexpected(shaped.error());
  }

  LayoutEntry layout{};
  layout.key = layout_key;
  layout.total_x_advance_26_6 = shaped->total_x_advance_26_6;
  layout.grapheme_count = shaped->grapheme_count;
  layout.placements.reserve(shaped->glyphs.size());

  for (const auto &placement : shaped->glyphs) {
    GlyphEntry *entry = nullptr;
    if (auto ensured = ensure_glyph(shaper, fonts, font_fingerprint, placement,
                                    size_px, flags, entry);
        !ensured) {
      return std::unexpected(ensured.error());
    }
    LayoutPlacement lp{};
    lp.glyph_id = placement.glyph_id;
    lp.font_index = placement.font_index;
    lp.cluster = placement.cluster;
    lp.x_advance_26_6 = placement.x_advance_26_6;
    lp.y_advance_26_6 = placement.y_advance_26_6;
    lp.x_offset_26_6 = placement.x_offset_26_6;
    lp.y_offset_26_6 = placement.y_offset_26_6;
    layout.placements.push_back(lp);
  }

  layout_lru_.push_front(layout_key);
  layout.lru = layout_lru_.begin();
  auto [ins, ok] = layouts_.emplace(layout_key, std::move(layout));
  if (!ok) {
    return std::unexpected(
        make_invalid("glyph_atlas_layout_collision", layout_key));
  }
  note_frame_use({}, layout_key);
  return materialize_run(ins->second, font_fingerprint, size_px, flags, false);
}

} // namespace instantee::typography
