#include <instantee/render_metal/metal_glyph_atlas.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace instantee::render_metal {
namespace {

[[nodiscard]] diagnostics::Error make_gpu(std::string_view code,
                                          std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::gpu_failure,
                            diagnostics::ErrorKind::gpu_failure, code, "metal",
                            message};
}

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "metal.glyph_atlas", message};
}

struct AtlasPagePayload final : render_core::GpuResourcePayload {
  detail::MetalOwner<MTL::Texture> texture{};
  std::uint64_t atlas_generation = 0;
  std::uint32_t page_index = 0;
};

struct FrameSubmission {
  std::uint64_t frame_id = 0;
  render_core::SubmissionContext submission;
  std::uint64_t atlas_generation = 0;
};

} // namespace

struct MetalGlyphAtlas::Impl {
  Device *device = nullptr;
  std::vector<render_core::GpuResourceHandle> pages{};
  std::uint64_t uploaded_generation = 0;
  std::vector<FrameSubmission> in_flight{};
};

MetalGlyphAtlas::MetalGlyphAtlas(
    std::unique_ptr<Impl> impl, std::unique_ptr<typography::GlyphAtlas> cpu,
    std::unique_ptr<render_core::GpuResourceRegistry> registry,
    MetalGlyphAtlasStats stats) noexcept
    : impl_(std::move(impl)), cpu_(std::move(cpu)),
      registry_(std::move(registry)), stats_(std::move(stats)) {}

MetalGlyphAtlas::MetalGlyphAtlas(MetalGlyphAtlas &&) noexcept = default;
MetalGlyphAtlas &
MetalGlyphAtlas::operator=(MetalGlyphAtlas &&) noexcept = default;
MetalGlyphAtlas::~MetalGlyphAtlas() = default;

std::expected<MetalGlyphAtlas, diagnostics::Error>
MetalGlyphAtlas::create(Device &device, MetalGlyphAtlasConfig config) {
  if (auto alive = device.ensure_alive(); !alive) {
    return std::unexpected(alive.error());
  }
  auto registry =
      render_core::GpuResourceRegistry::create(config.registry_capacity);
  if (!registry) {
    return std::unexpected(registry.error());
  }
  auto cpu = std::make_unique<typography::GlyphAtlas>(std::move(config.cpu));
  auto impl = std::make_unique<Impl>();
  impl->device = &device;
  MetalGlyphAtlasStats stats{};
  stats.cpu = cpu->stats();
  return MetalGlyphAtlas{
      std::move(impl), std::move(cpu),
      std::make_unique<render_core::GpuResourceRegistry>(std::move(*registry)),
      stats};
}

render_core::GpuResourceHandle
MetalGlyphAtlas::page_handle(std::uint32_t page) const noexcept {
  if (impl_ == nullptr || page >= impl_->pages.size()) {
    return {};
  }
  return impl_->pages[page];
}

std::expected<void, diagnostics::Error>
MetalGlyphAtlas::begin_frame(std::uint64_t frame_id) {
  if (!impl_ || !cpu_ || !registry_ || impl_->device == nullptr) {
    return std::unexpected(make_invalid("glyph_atlas_not_ready",
                                        "Metal glyph atlas is not ready"));
  }
  if (auto alive = impl_->device->ensure_alive(); !alive) {
    return std::unexpected(alive.error());
  }
  for (const auto &frame : impl_->in_flight) {
    if (frame.frame_id == frame_id) {
      return std::unexpected(
          make_invalid("glyph_atlas_frame_inflight",
                       std::format("frame already in flight: {}", frame_id)));
    }
  }

  if (auto begun = cpu_->begin_frame(frame_id); !begun) {
    return std::unexpected(begun.error());
  }

  FrameSubmission frame{
      .frame_id = frame_id,
      .submission = registry_->begin_submission(),
      .atlas_generation = cpu_->atlas_generation(),
  };
  for (const auto handle : impl_->pages) {
    if (!handle.valid()) {
      continue;
    }
    if (auto retained = frame.submission.retain(handle); !retained) {
      (void)frame.submission.cancel();
      (void)cpu_->end_frame(frame_id);
      return std::unexpected(retained.error());
    }
    ++stats_.gpu_retains;
  }
  impl_->in_flight.push_back(std::move(frame));
  stats_.cpu = cpu_->stats();
  return {};
}

std::expected<void, diagnostics::Error>
MetalGlyphAtlas::end_frame(std::uint64_t frame_id) {
  if (!impl_ || !cpu_ || !registry_) {
    return std::unexpected(make_invalid("glyph_atlas_not_ready",
                                        "Metal glyph atlas is not ready"));
  }
  const auto it = std::find_if(
      impl_->in_flight.begin(), impl_->in_flight.end(),
      [frame_id](const FrameSubmission &f) { return f.frame_id == frame_id; });
  if (it == impl_->in_flight.end()) {
    return std::unexpected(
        make_invalid("glyph_atlas_frame_missing",
                     std::format("frame not in flight: {}", frame_id)));
  }

  if (auto completed = it->submission.complete(); !completed) {
    return std::unexpected(completed.error());
  }
  impl_->in_flight.erase(it);

  if (auto ended = cpu_->end_frame(frame_id); !ended) {
    return std::unexpected(ended.error());
  }

  retire_stale_pages();
  stats_.cpu = cpu_->stats();
  return {};
}

void MetalGlyphAtlas::retire_stale_pages() noexcept {
  if (!impl_ || !registry_ || !cpu_) {
    return;
  }
  const std::uint64_t live_gen = cpu_->atlas_generation();
  bool any_hold = false;
  for (const auto &frame : impl_->in_flight) {
    if (frame.atlas_generation != live_gen) {
      any_hold = true;
      break;
    }
  }
  // Pages for the live generation stay; older generations are only retired
  // once no in-flight submission still pins them.
  if (any_hold) {
    return;
  }
  // Current pages vector always tracks the live generation after sync.
  (void)live_gen;
}

std::expected<void, diagnostics::Error>
MetalGlyphAtlas::ensure_page_texture(std::uint32_t page) {
  const auto *cpu_page = cpu_->cpu_atlas().page(page);
  if (cpu_page == nullptr) {
    return std::unexpected(
        make_invalid("glyph_atlas_page_missing",
                     std::format("CPU atlas page {} missing", page)));
  }
  if (page < impl_->pages.size() && impl_->pages[page].valid() &&
      impl_->uploaded_generation == cpu_->atlas_generation()) {
    auto payload = registry_->try_get_as<AtlasPagePayload>(impl_->pages[page]);
    if (payload && (*payload)->atlas_generation == cpu_->atlas_generation()) {
      return {};
    }
  }

  if (page >= impl_->pages.size()) {
    impl_->pages.resize(page + 1);
  }

  if (impl_->pages[page].valid()) {
    (void)registry_->request_retire(impl_->pages[page]);
    impl_->pages[page] = {};
    ++stats_.pages_retired;
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(make_gpu("autorelease_pool_failed",
                                    "Cannot create Metal autorelease pool"));
  }

  auto native = detail::native_handles(*impl_->device);
  detail::MetalOwner<MTL::TextureDescriptor> desc{
      MTL::TextureDescriptor::alloc()->init()};
  if (!desc) {
    return std::unexpected(make_gpu("texture_alloc_failed",
                                    "Cannot allocate glyph atlas descriptor"));
  }
  desc->setTextureType(MTL::TextureType2D);
  desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  desc->setWidth(cpu_page->width_px);
  desc->setHeight(cpu_page->height_px);
  desc->setStorageMode(MTL::StorageModeShared);
  desc->setUsage(MTL::TextureUsageShaderRead);
  detail::MetalOwner<MTL::Texture> texture{
      native.device->newTexture(desc.get())};
  if (!texture) {
    return std::unexpected(make_gpu("texture_create_failed",
                                    "Cannot create glyph atlas page texture"));
  }

  auto payload = std::make_unique<AtlasPagePayload>();
  payload->atlas_generation = cpu_->atlas_generation();
  payload->page_index = page;
  payload->texture = std::move(texture);

  render_core::GpuResourceDesc res_desc{};
  res_desc.width_px = cpu_page->width_px;
  res_desc.height_px = cpu_page->height_px;
  auto handle = registry_->create_resource(res_desc, std::move(payload));
  if (!handle) {
    return std::unexpected(handle.error());
  }
  if (auto uploading = registry_->begin_upload(*handle); !uploading) {
    (void)registry_->mark_failed(*handle);
    return std::unexpected(uploading.error());
  }
  impl_->pages[page] = *handle;
  ++stats_.pages_created;

  // Retain for the current in-flight frame if any.
  if (!impl_->in_flight.empty()) {
    auto &frame = impl_->in_flight.back();
    frame.atlas_generation = cpu_->atlas_generation();
    if (auto retained = frame.submission.retain(*handle); !retained) {
      return std::unexpected(retained.error());
    }
    ++stats_.gpu_retains;
  }
  return {};
}

std::expected<void, diagnostics::Error>
MetalGlyphAtlas::upload_batch(const asset_pipeline::AtlasUploadBatch &batch) {
  if (batch.regions.empty()) {
    ++stats_.warm_uploads_skipped;
    return {};
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(make_gpu("autorelease_pool_failed",
                                    "Cannot create Metal autorelease pool"));
  }

  std::vector<std::uint32_t> dirty_pages;
  dirty_pages.reserve(batch.regions.size());

  for (const auto &region : batch.regions) {
    if (auto ensured = ensure_page_texture(region.page); !ensured) {
      return ensured;
    }
    auto payload =
        registry_->try_get_as<AtlasPagePayload>(impl_->pages[region.page]);
    if (!payload) {
      return std::unexpected(payload.error());
    }
    MTL::Texture *texture = (*payload)->texture.get();
    if (texture == nullptr) {
      return std::unexpected(
          make_gpu("texture_missing", "Glyph atlas page texture is null"));
    }

    const std::size_t expected = static_cast<std::size_t>(region.width_px) *
                                 static_cast<std::size_t>(region.height_px) *
                                 4u;
    if (region.staging_offset + expected > batch.staging_rgba.size() ||
        region.staging_bytes != expected) {
      return std::unexpected(
          make_invalid("glyph_atlas_staging_bounds",
                       "atlas upload region staging bounds are invalid"));
    }

    const void *src = batch.staging_rgba.data() + region.staging_offset;
    const NS::UInteger bytes_per_row =
        static_cast<NS::UInteger>(region.width_px) * 4u;
    const MTL::Region mtl_region = MTL::Region::Make2D(
        region.x_px, region.y_px, region.width_px, region.height_px);
    texture->replaceRegion(mtl_region, /*level=*/0, src, bytes_per_row);

    if (std::find(dirty_pages.begin(), dirty_pages.end(), region.page) ==
        dirty_pages.end()) {
      dirty_pages.push_back(region.page);
    }
  }

  for (const std::uint32_t page : dirty_pages) {
    auto state = registry_->state(impl_->pages[page]);
    if (!state) {
      return std::unexpected(state.error());
    }
    if (*state == render_core::GpuResourceState::gpu_uploading) {
      if (auto resident = registry_->mark_resident(impl_->pages[page]);
          !resident) {
        return std::unexpected(resident.error());
      }
    }
  }

  ++stats_.upload_batches;
  stats_.upload_bytes += batch.staging_bytes;
  return {};
}

std::expected<void, diagnostics::Error> MetalGlyphAtlas::sync_gpu_pages() {
  if (impl_->uploaded_generation != cpu_->atlas_generation()) {
    // Generation bump: drop live page slots. Handles still retained by older
    // SubmissionContexts stay in `retiring` until those frames complete.
    for (auto &handle : impl_->pages) {
      if (!handle.valid()) {
        continue;
      }
      (void)registry_->request_retire(handle);
      ++stats_.pages_retired;
      handle = {};
    }
    impl_->pages.clear();
    impl_->uploaded_generation = cpu_->atlas_generation();
  }

  const std::uint32_t page_count =
      static_cast<std::uint32_t>(cpu_->cpu_atlas().page_count());
  for (std::uint32_t page = 0; page < page_count; ++page) {
    if (auto ensured = ensure_page_texture(page); !ensured) {
      return ensured;
    }
  }

  auto planned = cpu_->cpu_atlas().plan_uploads();
  if (!planned) {
    return std::unexpected(planned.error());
  }
  if (auto uploaded = upload_batch(*planned); !uploaded) {
    return uploaded;
  }
  cpu_->cpu_atlas().mark_uploads_resident(*planned);
  impl_->uploaded_generation = cpu_->atlas_generation();
  return {};
}

std::expected<typography::CachedGlyphRun, diagnostics::Error>
MetalGlyphAtlas::prepare(typography::TextShaper &shaper,
                         typography::FontStack &fonts,
                         std::string_view font_fingerprint,
                         std::string_view utf8, float size_px,
                         typography::GlyphRenderFlags flags) {
  if (!impl_ || !cpu_ || !registry_ || impl_->device == nullptr) {
    return std::unexpected(make_invalid("glyph_atlas_not_ready",
                                        "Metal glyph atlas is not ready"));
  }
  if (auto alive = impl_->device->ensure_alive(); !alive) {
    return std::unexpected(alive.error());
  }

  auto run =
      cpu_->prepare(shaper, fonts, font_fingerprint, utf8, size_px, flags);
  if (!run) {
    return std::unexpected(run.error());
  }
  if (auto synced = sync_gpu_pages(); !synced) {
    return std::unexpected(synced.error());
  }
  stats_.cpu = cpu_->stats();
  return run;
}

} // namespace instantee::render_metal
