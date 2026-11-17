#pragma once

#include <instantee/diagnostics/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace instantee::asset_pipeline {

// Sampling intent recorded with the atlas. Backends map these to GPU sampler
// state; the CPU atlas itself always stores tightly packed RGBA8.
enum class AtlasSampleFilter : std::uint8_t {
  nearest = 1,
  linear = 2,
};

enum class AtlasMipPolicy : std::uint8_t {
  // Single level only. Prefer for UI sprites that must not bleed across mips.
  none = 1,
  // Build a full box-filter mip chain on the atlas page after packing.
  generate_box = 2,
};

struct TextureAtlasConfig {
  std::uint32_t page_width_px = 256;
  std::uint32_t page_height_px = 256;
  // Clear gutter between sprites. Must be >= 1 to prevent bilinear bleed.
  std::uint32_t padding_px = 1;
  std::uint32_t max_pages = 8;
  std::uint32_t max_sprites = 4096;
  AtlasSampleFilter sample_filter = AtlasSampleFilter::linear;
  AtlasMipPolicy mip_policy = AtlasMipPolicy::none;
};

struct AtlasRect {
  std::uint32_t page = 0;
  std::uint32_t x_px = 0;
  std::uint32_t y_px = 0;
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
};

struct AtlasSprite {
  std::string content_key{};
  AtlasRect rect{};
  bool resident = false; // true after a successful upload flush covering it
};

struct TextureAtlasStats {
  std::uint64_t insert_hits = 0;
  std::uint64_t insert_misses = 0;
  std::uint64_t pack_failures = 0;
  std::uint64_t pages_allocated = 0;
  std::uint64_t sprites = 0;
  std::uint64_t staging_bytes_planned = 0;
  std::uint64_t staging_bytes_uploaded = 0;
  std::uint64_t upload_batches = 0;
  std::uint64_t upload_regions = 0;
  std::uint64_t resident_reuses = 0;
};

struct AtlasUploadRegion {
  std::uint32_t page = 0;
  std::uint32_t x_px = 0;
  std::uint32_t y_px = 0;
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  // Byte offset into the staged contiguous buffer produced by plan_uploads().
  std::size_t staging_offset = 0;
  std::size_t staging_bytes = 0;
};

struct AtlasUploadBatch {
  std::vector<AtlasUploadRegion> regions{};
  std::vector<std::uint8_t> staging_rgba{};
  std::uint64_t staging_bytes = 0;
};

struct AtlasMipLevel {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::vector<std::uint8_t> rgba{};
};

struct AtlasPage {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::vector<std::uint8_t> rgba{}; // level 0, tightly packed RGBA8
  std::vector<AtlasMipLevel>
      mips{}; // levels 1..n when mip_policy requests them
};

// CPU raster atlas: packs small RGBA sprites with a clear padding gutter,
// tracks content-key hits, and batches dirty-region uploads into a single
// staging buffer. GPU handles are never stored here.
class TextureAtlas {
public:
  explicit TextureAtlas(TextureAtlasConfig config = {});

  [[nodiscard]] const TextureAtlasConfig &config() const noexcept {
    return config_;
  }
  [[nodiscard]] const TextureAtlasStats &stats() const noexcept {
    return stats_;
  }
  [[nodiscard]] std::size_t page_count() const noexcept {
    return pages_.size();
  }
  [[nodiscard]] std::size_t sprite_count() const noexcept {
    return sprites_.size();
  }

  [[nodiscard]] const AtlasPage *page(std::uint32_t index) const noexcept;
  [[nodiscard]] const AtlasSprite *find(std::string_view content_key) const;

  // Pack `rgba` (tight width*height*4) under `content_key`. Repeated inserts
  // with the same key are hits and do not reallocate atlas space.
  [[nodiscard]] std::expected<const AtlasSprite *, diagnostics::Error>
  insert(std::string_view content_key, std::uint32_t width_px,
         std::uint32_t height_px, std::span<const std::uint8_t> rgba);

  // Copy exclusive sprite pixels (no padding) into `out` (size w*h*4).
  [[nodiscard]] std::expected<void, diagnostics::Error>
  copy_sprite_rgba(std::string_view content_key,
                   std::span<std::uint8_t> out) const;

  // Read a single atlas page texel. Used by bleed goldens.
  [[nodiscard]] std::expected<std::array<std::uint8_t, 4>, diagnostics::Error>
  sample_page(std::uint32_t page, std::uint32_t x_px, std::uint32_t y_px) const;

  // Build a contiguous staging buffer covering every dirty (non-resident)
  // sprite. Empty when everything is already resident — callers must not
  // reinstantiate GPU uploads each frame.
  [[nodiscard]] std::expected<AtlasUploadBatch, diagnostics::Error>
  plan_uploads();

  // Mark planned sprites resident after a successful GPU upload. Idempotent.
  void mark_uploads_resident(const AtlasUploadBatch &batch) noexcept;

  void clear() noexcept;

private:
  struct ShelfCursor {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t row_height = 0;
  };

  [[nodiscard]] std::expected<AtlasRect, diagnostics::Error>
  pack_rect(std::uint32_t width_px, std::uint32_t height_px);

  [[nodiscard]] std::expected<void, diagnostics::Error>
  blit_sprite(const AtlasRect &rect, std::span<const std::uint8_t> rgba);

  void rebuild_mips(std::uint32_t page_index);
  void mark_dirty(const AtlasRect &rect) noexcept;

  TextureAtlasConfig config_{};
  TextureAtlasStats stats_{};
  std::vector<AtlasPage> pages_{};
  std::vector<ShelfCursor> cursors_{};
  std::unordered_map<std::string, AtlasSprite> sprites_{};
  // Dirty exclusive sprite rects awaiting upload (padding not uploaded).
  std::vector<AtlasRect> dirty_{};
};

} // namespace instantee::asset_pipeline
