#include <instantee/asset_pipeline/texture_atlas.hpp>

#include <algorithm>
#include <limits>

namespace instantee::asset_pipeline {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "texture_atlas", message};
}

[[nodiscard]] diagnostics::Error make_budget(std::string_view code,
                                             std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::resource_budget_exceeded,
                            diagnostics::ErrorKind::resource_budget_exceeded,
                            code, "texture_atlas", message};
}

[[nodiscard]] std::size_t rgba_bytes(std::uint32_t w, std::uint32_t h) {
  return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U;
}

void box_filter_mip(const std::vector<std::uint8_t> &src, std::uint32_t sw,
                    std::uint32_t sh, std::vector<std::uint8_t> &dst,
                    std::uint32_t dw, std::uint32_t dh) {
  dst.assign(rgba_bytes(dw, dh), 0);
  for (std::uint32_t y = 0; y < dh; ++y) {
    for (std::uint32_t x = 0; x < dw; ++x) {
      const std::uint32_t x0 = x * 2U;
      const std::uint32_t y0 = y * 2U;
      const std::uint32_t x1 = std::min(x0 + 1U, sw - 1U);
      const std::uint32_t y1 = std::min(y0 + 1U, sh - 1U);
      for (int c = 0; c < 4; ++c) {
        const std::uint32_t sum =
            src[(static_cast<std::size_t>(y0) * sw + x0) * 4U +
                static_cast<std::size_t>(c)] +
            src[(static_cast<std::size_t>(y0) * sw + x1) * 4U +
                static_cast<std::size_t>(c)] +
            src[(static_cast<std::size_t>(y1) * sw + x0) * 4U +
                static_cast<std::size_t>(c)] +
            src[(static_cast<std::size_t>(y1) * sw + x1) * 4U +
                static_cast<std::size_t>(c)];
        dst[(static_cast<std::size_t>(y) * dw + x) * 4U +
            static_cast<std::size_t>(c)] =
            static_cast<std::uint8_t>((sum + 2U) / 4U);
      }
    }
  }
}

} // namespace

TextureAtlas::TextureAtlas(TextureAtlasConfig config) : config_(config) {}

const AtlasPage *TextureAtlas::page(std::uint32_t index) const noexcept {
  if (index >= pages_.size()) {
    return nullptr;
  }
  return &pages_[index];
}

const AtlasSprite *TextureAtlas::find(std::string_view content_key) const {
  const auto it = sprites_.find(std::string(content_key));
  if (it == sprites_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::expected<AtlasRect, diagnostics::Error>
TextureAtlas::pack_rect(std::uint32_t width_px, std::uint32_t height_px) {
  const std::uint32_t pad = config_.padding_px;
  if (width_px == 0 || height_px == 0) {
    return std::unexpected(
        make_invalid("atlas_empty_sprite", "sprite dimensions must be > 0"));
  }
  if (width_px + 2U * pad > config_.page_width_px ||
      height_px + 2U * pad > config_.page_height_px) {
    return std::unexpected(
        make_budget("atlas_sprite_too_large",
                    "sprite plus padding does not fit a single atlas page"));
  }

  auto try_place = [&](std::uint32_t page_index) -> std::optional<AtlasRect> {
    ShelfCursor &cur = cursors_[page_index];
    // Ensure left/top gutter: cursor starts at padding.
    if (cur.x == 0 && cur.y == 0) {
      cur.x = pad;
      cur.y = pad;
    }
    if (cur.x + width_px + pad > config_.page_width_px) {
      cur.x = pad;
      cur.y += cur.row_height + pad;
      cur.row_height = 0;
    }
    if (cur.y + height_px + pad > config_.page_height_px) {
      return std::nullopt;
    }
    AtlasRect rect{.page = page_index,
                   .x_px = cur.x,
                   .y_px = cur.y,
                   .width_px = width_px,
                   .height_px = height_px};
    cur.x += width_px + pad;
    cur.row_height = std::max(cur.row_height, height_px);
    return rect;
  };

  for (std::uint32_t i = 0; i < pages_.size(); ++i) {
    if (auto placed = try_place(i)) {
      return *placed;
    }
  }

  if (pages_.size() >= config_.max_pages) {
    ++stats_.pack_failures;
    return std::unexpected(make_budget(
        "atlas_pages_exhausted", "atlas page budget exhausted while packing"));
  }

  AtlasPage page;
  page.width_px = config_.page_width_px;
  page.height_px = config_.page_height_px;
  page.rgba.assign(rgba_bytes(page.width_px, page.height_px), 0);
  pages_.push_back(std::move(page));
  cursors_.push_back({});
  ++stats_.pages_allocated;

  const std::uint32_t page_index =
      static_cast<std::uint32_t>(pages_.size() - 1U);
  auto placed = try_place(page_index);
  if (!placed) {
    ++stats_.pack_failures;
    return std::unexpected(
        make_budget("atlas_pack_failed",
                    "failed to place sprite on a freshly allocated page"));
  }
  return *placed;
}

std::expected<void, diagnostics::Error>
TextureAtlas::blit_sprite(const AtlasRect &rect,
                          std::span<const std::uint8_t> rgba) {
  if (rect.page >= pages_.size()) {
    return std::unexpected(
        make_invalid("atlas_bad_page", "sprite page index is out of range"));
  }
  AtlasPage &page = pages_[rect.page];
  const std::size_t expected = rgba_bytes(rect.width_px, rect.height_px);
  if (rgba.size() != expected) {
    return std::unexpected(make_invalid(
        "atlas_rgba_size", "sprite rgba size must equal width*height*4"));
  }
  for (std::uint32_t row = 0; row < rect.height_px; ++row) {
    const std::size_t src = static_cast<std::size_t>(row) * rect.width_px * 4U;
    const std::size_t dst =
        (static_cast<std::size_t>(rect.y_px + row) * page.width_px +
         rect.x_px) *
        4U;
    std::copy_n(rgba.data() + src, rect.width_px * 4U, page.rgba.data() + dst);
  }
  return {};
}

void TextureAtlas::rebuild_mips(std::uint32_t page_index) {
  if (config_.mip_policy != AtlasMipPolicy::generate_box) {
    return;
  }
  AtlasPage &page = pages_[page_index];
  page.mips.clear();
  std::uint32_t w = page.width_px;
  std::uint32_t h = page.height_px;
  const std::vector<std::uint8_t> *src = &page.rgba;
  while (w > 1U || h > 1U) {
    const std::uint32_t nw = std::max(1U, w / 2U);
    const std::uint32_t nh = std::max(1U, h / 2U);
    AtlasMipLevel level;
    level.width_px = nw;
    level.height_px = nh;
    box_filter_mip(*src, w, h, level.rgba, nw, nh);
    page.mips.push_back(std::move(level));
    src = &page.mips.back().rgba;
    w = nw;
    h = nh;
  }
}

void TextureAtlas::mark_dirty(const AtlasRect &rect) noexcept {
  dirty_.push_back(rect);
}

std::expected<const AtlasSprite *, diagnostics::Error>
TextureAtlas::insert(std::string_view content_key, std::uint32_t width_px,
                     std::uint32_t height_px,
                     std::span<const std::uint8_t> rgba) {
  if (content_key.empty()) {
    return std::unexpected(
        make_invalid("atlas_empty_key", "content key must be non-empty"));
  }
  if (config_.padding_px == 0 || config_.page_width_px == 0 ||
      config_.page_height_px == 0 || config_.max_pages == 0 ||
      config_.max_sprites == 0) {
    return std::unexpected(make_invalid(
        "atlas_bad_config",
        "atlas config requires positive page size, padding>=1, and limits"));
  }

  const std::string key(content_key);
  if (auto it = sprites_.find(key); it != sprites_.end()) {
    ++stats_.insert_hits;
    if (it->second.resident) {
      ++stats_.resident_reuses;
    }
    return &it->second;
  }

  if (sprites_.size() >= config_.max_sprites) {
    ++stats_.pack_failures;
    return std::unexpected(
        make_budget("atlas_sprite_limit", "atlas sprite count limit reached"));
  }

  ++stats_.insert_misses;
  auto rect = pack_rect(width_px, height_px);
  if (!rect) {
    return std::unexpected(rect.error());
  }
  auto blit = blit_sprite(*rect, rgba);
  if (!blit) {
    return std::unexpected(blit.error());
  }
  rebuild_mips(rect->page);
  mark_dirty(*rect);

  AtlasSprite sprite;
  sprite.content_key = key;
  sprite.rect = *rect;
  sprite.resident = false;
  auto [ins, ok] = sprites_.emplace(key, std::move(sprite));
  (void)ok;
  ++stats_.sprites;
  return &ins->second;
}

std::expected<void, diagnostics::Error>
TextureAtlas::copy_sprite_rgba(std::string_view content_key,
                               std::span<std::uint8_t> out) const {
  const AtlasSprite *sprite = find(content_key);
  if (sprite == nullptr) {
    return std::unexpected(make_invalid("atlas_missing_sprite",
                                        "content key is not in the atlas"));
  }
  const AtlasPage *pg = page(sprite->rect.page);
  if (pg == nullptr) {
    return std::unexpected(
        make_invalid("atlas_bad_page", "sprite page index is out of range"));
  }
  const std::size_t expected =
      rgba_bytes(sprite->rect.width_px, sprite->rect.height_px);
  if (out.size() != expected) {
    return std::unexpected(make_invalid(
        "atlas_rgba_size", "output buffer must equal width*height*4"));
  }
  for (std::uint32_t row = 0; row < sprite->rect.height_px; ++row) {
    const std::size_t dst =
        static_cast<std::size_t>(row) * sprite->rect.width_px * 4U;
    const std::size_t src =
        (static_cast<std::size_t>(sprite->rect.y_px + row) * pg->width_px +
         sprite->rect.x_px) *
        4U;
    std::copy_n(pg->rgba.data() + src, sprite->rect.width_px * 4U,
                out.data() + dst);
  }
  return {};
}

std::expected<std::array<std::uint8_t, 4>, diagnostics::Error>
TextureAtlas::sample_page(std::uint32_t page_index, std::uint32_t x_px,
                          std::uint32_t y_px) const {
  const AtlasPage *pg = page(page_index);
  if (pg == nullptr) {
    return std::unexpected(
        make_invalid("atlas_bad_page", "page index is out of range"));
  }
  if (x_px >= pg->width_px || y_px >= pg->height_px) {
    return std::unexpected(make_invalid("atlas_sample_oob",
                                        "sample coordinates are out of range"));
  }
  const std::size_t offset =
      (static_cast<std::size_t>(y_px) * pg->width_px + x_px) * 4U;
  return std::array<std::uint8_t, 4>{pg->rgba[offset], pg->rgba[offset + 1],
                                     pg->rgba[offset + 2],
                                     pg->rgba[offset + 3]};
}

std::expected<AtlasUploadBatch, diagnostics::Error>
TextureAtlas::plan_uploads() {
  AtlasUploadBatch batch;
  if (dirty_.empty()) {
    return batch;
  }

  // Coalesce per dirty rect into one staging stream (upload batching).
  std::uint64_t total = 0;
  for (const AtlasRect &rect : dirty_) {
    const std::uint64_t bytes =
        static_cast<std::uint64_t>(rect.width_px) * rect.height_px * 4U;
    if (total > (std::numeric_limits<std::uint64_t>::max)() - bytes) {
      return std::unexpected(make_budget("atlas_staging_overflow",
                                         "staging byte counter overflowed"));
    }
    total += bytes;
  }
  if (total > (std::numeric_limits<std::size_t>::max)()) {
    return std::unexpected(make_budget("atlas_staging_too_large",
                                       "staging buffer exceeds size_t"));
  }
  batch.staging_rgba.resize(static_cast<std::size_t>(total));
  batch.staging_bytes = total;
  batch.regions.reserve(dirty_.size());

  std::size_t offset = 0;
  for (const AtlasRect &rect : dirty_) {
    const AtlasPage *pg = page(rect.page);
    if (pg == nullptr) {
      return std::unexpected(
          make_invalid("atlas_bad_page", "dirty region page is out of range"));
    }
    AtlasUploadRegion region;
    region.page = rect.page;
    region.x_px = rect.x_px;
    region.y_px = rect.y_px;
    region.width_px = rect.width_px;
    region.height_px = rect.height_px;
    region.staging_offset = offset;
    region.staging_bytes = rgba_bytes(rect.width_px, rect.height_px);
    for (std::uint32_t row = 0; row < rect.height_px; ++row) {
      const std::size_t src =
          (static_cast<std::size_t>(rect.y_px + row) * pg->width_px +
           rect.x_px) *
          4U;
      std::copy_n(pg->rgba.data() + src, rect.width_px * 4U,
                  batch.staging_rgba.data() + offset);
      offset += static_cast<std::size_t>(rect.width_px) * 4U;
    }
    batch.regions.push_back(region);
  }

  stats_.staging_bytes_planned += batch.staging_bytes;
  stats_.upload_batches += 1;
  stats_.upload_regions += batch.regions.size();
  return batch;
}

void TextureAtlas::mark_uploads_resident(
    const AtlasUploadBatch &batch) noexcept {
  if (batch.regions.empty()) {
    return;
  }
  stats_.staging_bytes_uploaded += batch.staging_bytes;
  for (auto &[_, sprite] : sprites_) {
    for (const AtlasUploadRegion &region : batch.regions) {
      if (sprite.rect.page == region.page && sprite.rect.x_px == region.x_px &&
          sprite.rect.y_px == region.y_px &&
          sprite.rect.width_px == region.width_px &&
          sprite.rect.height_px == region.height_px) {
        sprite.resident = true;
      }
    }
  }
  dirty_.clear();
}

void TextureAtlas::clear() noexcept {
  pages_.clear();
  cursors_.clear();
  sprites_.clear();
  dirty_.clear();
  stats_ = {};
}

} // namespace instantee::asset_pipeline
