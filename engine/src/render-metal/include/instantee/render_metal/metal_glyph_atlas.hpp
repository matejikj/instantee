#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/resource_registry.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/typography/glyph_atlas.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

namespace instantee::render_metal {

struct MetalGlyphAtlasConfig {
  typography::GlyphAtlasConfig cpu{};
  std::uint32_t registry_capacity = 16;
};

struct MetalGlyphAtlasStats {
  typography::GlyphAtlasStats cpu{};
  std::uint64_t pages_created = 0;
  std::uint64_t pages_retired = 0;
  std::uint64_t upload_batches = 0;
  std::uint64_t upload_bytes = 0;
  std::uint64_t warm_uploads_skipped = 0;
  std::uint64_t gpu_retains = 0;
};

// Metal residency layer over the CPU glyph atlas. Page textures are retained
// for every in-flight frame via SubmissionContext so eviction cannot free GPU
// memory still referenced by outstanding draws.
class MetalGlyphAtlas {
public:
  MetalGlyphAtlas(MetalGlyphAtlas &&) noexcept;
  MetalGlyphAtlas &operator=(MetalGlyphAtlas &&) noexcept;
  ~MetalGlyphAtlas();

  MetalGlyphAtlas(const MetalGlyphAtlas &) = delete;
  MetalGlyphAtlas &operator=(const MetalGlyphAtlas &) = delete;

  [[nodiscard]] static std::expected<MetalGlyphAtlas, diagnostics::Error>
  create(Device &device, MetalGlyphAtlasConfig config = {});

  [[nodiscard]] const MetalGlyphAtlasStats &stats() const noexcept {
    return stats_;
  }
  [[nodiscard]] typography::GlyphAtlas &cpu() noexcept { return *cpu_; }
  [[nodiscard]] const typography::GlyphAtlas &cpu() const noexcept {
    return *cpu_;
  }
  [[nodiscard]] render_core::GpuResourceRegistry &registry() noexcept {
    return *registry_;
  }

  [[nodiscard]] std::expected<void, diagnostics::Error>
  begin_frame(std::uint64_t frame_id);

  // Completes the frame submission (releases retains) then ends the CPU pin.
  [[nodiscard]] std::expected<void, diagnostics::Error>
  end_frame(std::uint64_t frame_id);

  // Shape/raster with CPU cache, then upload dirty atlas pages. Warm frames
  // with an unchanged atlas generation and resident sprites upload nothing.
  [[nodiscard]] std::expected<typography::CachedGlyphRun, diagnostics::Error>
  prepare(typography::TextShaper &shaper, typography::FontStack &fonts,
          std::string_view font_fingerprint, std::string_view utf8,
          float size_px, typography::GlyphRenderFlags flags = {});

  // Handle for atlas page `page` after a successful prepare/upload. Invalid
  // when the page does not exist for the current atlas generation.
  [[nodiscard]] render_core::GpuResourceHandle
  page_handle(std::uint32_t page) const noexcept;

private:
  struct Impl;

  MetalGlyphAtlas(std::unique_ptr<Impl> impl,
                  std::unique_ptr<typography::GlyphAtlas> cpu,
                  std::unique_ptr<render_core::GpuResourceRegistry> registry,
                  MetalGlyphAtlasStats stats) noexcept;

  [[nodiscard]] std::expected<void, diagnostics::Error> sync_gpu_pages();
  [[nodiscard]] std::expected<void, diagnostics::Error>
  ensure_page_texture(std::uint32_t page);
  [[nodiscard]] std::expected<void, diagnostics::Error>
  upload_batch(const asset_pipeline::AtlasUploadBatch &batch);
  void retire_stale_pages() noexcept;

  std::unique_ptr<Impl> impl_;
  std::unique_ptr<typography::GlyphAtlas> cpu_;
  std::unique_ptr<render_core::GpuResourceRegistry> registry_;
  MetalGlyphAtlasStats stats_{};
};

} // namespace instantee::render_metal
