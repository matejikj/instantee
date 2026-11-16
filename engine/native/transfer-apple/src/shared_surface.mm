#include <instantee/transfer_apple/shared_surface.hpp>

#include <instantee/render_metal/frame.hpp>

#include "cf_owner.hpp"
#include "device_native.hpp"
#include "metal_owner.hpp"
#include "pixel_buffer_native.hpp"
#include "shared_surface_pattern_source.hpp"

#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

#if !__has_feature(objc_arc)
#error "The Apple transfer bridge requires ARC"
#endif

namespace instantee::spike::transfer {
namespace {

constexpr const char *kPhase = "shared_surface";
constexpr std::uint32_t kWidth = metal::kTestPatternWidth;
constexpr std::uint32_t kHeight = metal::kTestPatternHeight;
constexpr std::size_t kChannels = metal::kTestPatternChannels;

[[nodiscard]] const char *ns_error_message(NS::Error *error) {
  if (error == nullptr || error->localizedDescription() == nullptr) {
    return "unknown Metal error";
  }
  const char *utf8 = error->localizedDescription()->utf8String();
  return utf8 == nullptr ? "unknown Metal error" : utf8;
}

} // namespace

struct SharedSurfaceLease::Impl {
  metal::detail::MetalOwner<MTL::CommandBuffer> command;
  detail::CfOwner<CVMetalTextureRef> cv_texture;
  std::optional<apple::PixelBuffer> buffer;
  CVMetalTextureCacheRef cache = nullptr; // borrowed; renderer outlives leases
  std::uint32_t frame = 0;
  State state = State::in_flight;

  void wait_for_gpu() noexcept {
    if (!command || state != State::in_flight) {
      return;
    }
    command->waitUntilCompleted();
    if (command->status() == MTL::CommandBufferStatusError) {
      state = State::failed;
      return;
    }
    state = State::ready;
  }

  void retire_gpu() noexcept {
    if (!command) {
      return;
    }
    const auto status = command->status();
    if (status != MTL::CommandBufferStatusCompleted &&
        status != MTL::CommandBufferStatusError) {
      command->waitUntilCompleted();
    }
    if (state == State::in_flight) {
      if (command->status() == MTL::CommandBufferStatusError) {
        state = State::failed;
      } else {
        state = State::ready;
      }
    }
    // Drop the Metal texture view before returning the pixel buffer to the
    // pool so a later acquire cannot observe a still-bound cache entry.
    cv_texture.reset();
    if (cache != nullptr) {
      CVMetalTextureCacheFlush(cache, 0);
    }
    command.reset();
  }
};

SharedSurfaceLease::SharedSurfaceLease(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

SharedSurfaceLease::SharedSurfaceLease(SharedSurfaceLease &&) noexcept =
    default;
SharedSurfaceLease &
SharedSurfaceLease::operator=(SharedSurfaceLease &&) noexcept = default;

SharedSurfaceLease::~SharedSurfaceLease() {
  if (!impl_) {
    return;
  }
  impl_->retire_gpu();
}

SharedSurfaceLease::State SharedSurfaceLease::state() const noexcept {
  return impl_ ? impl_->state : State::consumed;
}

std::uint32_t SharedSurfaceLease::frame_index() const noexcept {
  return impl_ ? impl_->frame : 0U;
}

void SharedSurfaceLease::cancel() noexcept {
  if (!impl_) {
    return;
  }
  if (impl_->state == State::in_flight || impl_->state == State::ready) {
    impl_->state = State::cancelled;
  }
}

std::expected<std::vector<std::uint8_t>, Error>
SharedSurfaceLease::take_bgra8() {
  if (!impl_ || !impl_->buffer ||
      apple::detail::pixel_buffer_empty(*impl_->buffer) || !impl_->command) {
    return std::unexpected(Error{ExitCode::gpu_failure, "lease_not_ready",
                                 kPhase, "Shared-surface lease is not ready"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                 "Shared-surface lease was cancelled"});
  }
  if (impl_->state == State::consumed) {
    return std::unexpected(Error{ExitCode::invalid_input, "already_consumed",
                                 kPhase,
                                 "Shared-surface pixels were already taken"});
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", kPhase,
              "Metal command buffer completed with an error"});
  }
  if (impl_->state == State::in_flight) {
    impl_->wait_for_gpu();
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", kPhase,
              "Metal command buffer completed with an error"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                 "Shared-surface lease was cancelled"});
  }

  // Texture view must be released before CPU lock so the surface is idle.
  impl_->cv_texture.reset();
  if (impl_->cache != nullptr) {
    CVMetalTextureCacheFlush(impl_->cache, 0);
  }

  CVPixelBufferRef cv_buffer =
      apple::detail::borrow_cv_pixel_buffer(*impl_->buffer);
  if (cv_buffer == nullptr) {
    return std::unexpected(Error{ExitCode::export_failure, "buffer_missing",
                                 kPhase, "Shared-surface buffer is missing"});
  }
  if (CVPixelBufferLockBaseAddress(cv_buffer, kCVPixelBufferLock_ReadOnly) !=
      kCVReturnSuccess) {
    return std::unexpected(Error{ExitCode::export_failure, "lock_failed",
                                 kPhase,
                                 "Cannot lock shared-surface base address"});
  }

  const auto *source =
      static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddress(cv_buffer));
  const std::size_t bytes_per_row =
      static_cast<std::size_t>(CVPixelBufferGetBytesPerRow(cv_buffer));
  const std::size_t tight_row = static_cast<std::size_t>(kWidth) * kChannels;
  const std::size_t pixel_bytes = tight_row * static_cast<std::size_t>(kHeight);
  if (source == nullptr) {
    CVPixelBufferUnlockBaseAddress(cv_buffer, kCVPixelBufferLock_ReadOnly);
    return std::unexpected(Error{ExitCode::export_failure, "map_failed", kPhase,
                                 "Shared-surface contents are unavailable"});
  }

  std::vector<std::uint8_t> bgra;
  try {
    bgra.resize(pixel_bytes);
  } catch (const std::bad_alloc &) {
    CVPixelBufferUnlockBaseAddress(cv_buffer, kCVPixelBufferLock_ReadOnly);
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory", kPhase,
                                 "Cannot allocate BGRA storage"});
  }
  for (std::uint32_t y = 0; y < kHeight; ++y) {
    std::memcpy(bgra.data() + static_cast<std::size_t>(y) * tight_row,
                source + static_cast<std::size_t>(y) * bytes_per_row,
                tight_row);
  }
  if (CVPixelBufferUnlockBaseAddress(cv_buffer, kCVPixelBufferLock_ReadOnly) !=
      kCVReturnSuccess) {
    return std::unexpected(Error{ExitCode::export_failure, "unlock_failed",
                                 kPhase,
                                 "Cannot unlock shared-surface base address"});
  }

  impl_->state = State::consumed;
  impl_->command.reset();
  return bgra;
}

std::expected<apple::PixelBuffer, Error>
SharedSurfaceLease::take_pixel_buffer() {
  if (!impl_ || !impl_->buffer ||
      apple::detail::pixel_buffer_empty(*impl_->buffer) || !impl_->command) {
    return std::unexpected(Error{ExitCode::gpu_failure, "lease_not_ready",
                                 kPhase, "Shared-surface lease is not ready"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                 "Shared-surface lease was cancelled"});
  }
  if (impl_->state == State::consumed) {
    return std::unexpected(Error{ExitCode::invalid_input, "already_consumed",
                                 kPhase,
                                 "Shared-surface buffer was already taken"});
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", kPhase,
              "Metal command buffer completed with an error"});
  }
  if (impl_->state == State::in_flight) {
    impl_->wait_for_gpu();
  }
  if (impl_->state == State::failed) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "gpu_command_failed", kPhase,
              "Metal command buffer completed with an error"});
  }
  if (impl_->state == State::cancelled) {
    return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                 "Shared-surface lease was cancelled"});
  }

  impl_->cv_texture.reset();
  if (impl_->cache != nullptr) {
    CVMetalTextureCacheFlush(impl_->cache, 0);
  }
  impl_->command.reset();
  impl_->buffer->seal();
  impl_->state = State::consumed;
  apple::PixelBuffer owned = std::move(*impl_->buffer);
  impl_->buffer.reset();
  return owned;
}

struct SharedSurfaceRenderer::Impl {
  MTL::Device *device = nullptr;
  MTL::CommandQueue *queue = nullptr;
  metal::detail::MetalOwner<MTL::RenderPipelineState> pipeline;
  detail::CfOwner<CVMetalTextureCacheRef> texture_cache;
  std::unique_ptr<apple::PixelBufferPool> pool;
  std::uint32_t capacity = 0;
};

SharedSurfaceRenderer::SharedSurfaceRenderer(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

SharedSurfaceRenderer::SharedSurfaceRenderer(
    SharedSurfaceRenderer &&) noexcept = default;
SharedSurfaceRenderer &
SharedSurfaceRenderer::operator=(SharedSurfaceRenderer &&) noexcept = default;
SharedSurfaceRenderer::~SharedSurfaceRenderer() = default;

std::expected<SharedSurfaceRenderer, Error>
SharedSurfaceRenderer::create(metal::Device &device, std::uint32_t capacity) {
  if (capacity == 0U) {
    return std::unexpected(Error{ExitCode::invalid_input, "invalid_capacity",
                                 kPhase,
                                 "Shared-surface pool capacity must be > 0"});
  }

  metal::detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", kPhase,
                                 "Cannot create Metal autorelease pool"});
  }

  const metal::detail::DeviceNative native =
      metal::detail::native_handles(device);
  if (native.device == nullptr || native.queue == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metal_device_unavailable", kPhase,
                                 "Metal device handles are unavailable"});
  }

  CVMetalTextureCacheRef raw_cache = nullptr;
  const CVReturn cache_status = CVMetalTextureCacheCreate(
      kCFAllocatorDefault, nullptr, (__bridge id<MTLDevice>)native.device,
      nullptr, &raw_cache);
  detail::CfOwner<CVMetalTextureCacheRef> texture_cache =
      detail::adopt_cf(raw_cache);
  if (cache_status != kCVReturnSuccess || !texture_cache) {
    return std::unexpected(Error{ExitCode::gpu_failure, "texture_cache_failed",
                                 kPhase, "Cannot create CVMetalTextureCache"});
  }

  auto pixel_pool = apple::PixelBufferPool::create(
      apple::PixelBufferFormat::media_1080x1920(), capacity,
      apple::PixelBufferCompatibility::metal_iosurface);
  if (!pixel_pool) {
    return std::unexpected(pixel_pool.error());
  }

  NS::Error *error = nullptr;
  NS::String *source =
      NS::String::string(detail::kSharedSurfacePatternSource,
                         NS::StringEncoding::UTF8StringEncoding);
  if (source == nullptr) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "shader_source_failed", kPhase,
              "Cannot allocate shared-surface shader source"});
  }
  metal::detail::MetalOwner<MTL::Library> library{
      native.device->newLibrary(source, nullptr, &error)};
  if (!library) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure, "shader_compile_failed",
                                 kPhase, "Cannot compile shared-surface MSL"});
  }

  metal::detail::MetalOwner<MTL::Function> vertex{
      library->newFunction(NS::String::string(
          "vs_shared_main", NS::StringEncoding::UTF8StringEncoding))};
  metal::detail::MetalOwner<MTL::Function> fragment{
      library->newFunction(NS::String::string(
          "fs_shared_main", NS::StringEncoding::UTF8StringEncoding))};
  if (!vertex || !fragment) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "shader_entry_missing", kPhase,
              "Shared-surface shader entry points are missing"});
  }

  metal::detail::MetalOwner<MTL::RenderPipelineDescriptor> pipeline_desc{
      MTL::RenderPipelineDescriptor::alloc()->init()};
  if (!pipeline_desc) {
    return std::unexpected(Error{ExitCode::gpu_failure, "pipeline_alloc_failed",
                                 kPhase,
                                 "Cannot allocate shared-surface pipeline"});
  }
  pipeline_desc->setVertexFunction(vertex.get());
  pipeline_desc->setFragmentFunction(fragment.get());
  auto *attachment = pipeline_desc->colorAttachments()->object(0);
  attachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  attachment->setBlendingEnabled(false);

  error = nullptr;
  metal::detail::MetalOwner<MTL::RenderPipelineState> pipeline{
      native.device->newRenderPipelineState(pipeline_desc.get(), &error)};
  if (!pipeline) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_create_failed", kPhase,
                                 "Cannot create shared-surface pipeline"});
  }

  auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory", kPhase,
                                 "Cannot allocate shared-surface renderer"});
  }
  impl->device = native.device;
  impl->queue = native.queue;
  impl->pipeline = std::move(pipeline);
  impl->texture_cache = std::move(texture_cache);
  impl->pool = std::make_unique<apple::PixelBufferPool>(std::move(*pixel_pool));
  impl->capacity = capacity;
  return SharedSurfaceRenderer{std::move(impl)};
}

apple::PixelBufferCompatibility
SharedSurfaceRenderer::compatibility() const noexcept {
  return impl_ && impl_->pool
             ? impl_->pool->compatibility()
             : apple::PixelBufferCompatibility::encoder_cpu_fill;
}

std::uint32_t SharedSurfaceRenderer::capacity() const noexcept {
  return impl_ ? impl_->capacity : 0U;
}

std::expected<SharedSurfaceLease, Error> SharedSurfaceRenderer::submit(
    std::uint32_t frame_index, std::stop_token stop,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
  if (frame_index > 1U) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "unsupported_frame", kPhase,
              "Shared-surface pattern supports frames 0 and 1"});
  }
  if (!impl_ || impl_->device == nullptr || impl_->queue == nullptr ||
      !impl_->pipeline || !impl_->texture_cache || !impl_->pool) {
    return std::unexpected(Error{ExitCode::gpu_failure, "renderer_not_ready",
                                 kPhase,
                                 "Shared-surface renderer is not ready"});
  }

  metal::detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", kPhase,
                                 "Cannot create Metal autorelease pool"});
  }

  auto buffer = impl_->pool->acquire(std::move(stop), deadline);
  if (!buffer) {
    return std::unexpected(buffer.error());
  }
  if (buffer->sealed()) {
    return std::unexpected(Error{ExitCode::export_failure, "sealed_buffer",
                                 kPhase,
                                 "Acquired shared-surface buffer is sealed"});
  }

  CVPixelBufferRef cv_buffer = apple::detail::borrow_cv_pixel_buffer(*buffer);
  if (cv_buffer == nullptr || CVPixelBufferGetIOSurface(cv_buffer) == nullptr) {
    return std::unexpected(
        Error{ExitCode::export_failure, "iosurface_missing", kPhase,
              "Shared-surface buffer is not IOSurface-backed"});
  }

  CVMetalTextureRef raw_texture = nullptr;
  const CVReturn texture_status = CVMetalTextureCacheCreateTextureFromImage(
      kCFAllocatorDefault, impl_->texture_cache.get(), cv_buffer, nullptr,
      MTLPixelFormatBGRA8Unorm, kWidth, kHeight, 0, &raw_texture);
  detail::CfOwner<CVMetalTextureRef> cv_texture = detail::adopt_cf(raw_texture);
  id<MTLTexture> objc_texture =
      cv_texture ? CVMetalTextureGetTexture(cv_texture.get()) : nil;
  if (texture_status != kCVReturnSuccess || objc_texture == nil) {
    return std::unexpected(Error{ExitCode::gpu_failure, "texture_wrap_failed",
                                 kPhase,
                                 "Cannot wrap IOSurface as a Metal texture"});
  }
  auto *mtl_texture = (__bridge MTL::Texture *)objc_texture;

  MTL::CommandBuffer *raw_command = impl_->queue->commandBuffer();
  if (raw_command == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 kPhase,
                                 "Cannot create a Metal command buffer"});
  }
  metal::detail::MetalOwner<MTL::CommandBuffer> command =
      metal::detail::retain_owner(raw_command);

  metal::detail::MetalOwner<MTL::RenderPassDescriptor> pass{
      MTL::RenderPassDescriptor::alloc()->init()};
  if (!pass) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_pass_failed",
                                 kPhase,
                                 "Cannot allocate shared-surface render pass"});
  }
  auto *color = pass->colorAttachments()->object(0);
  color->setTexture(mtl_texture);
  color->setLoadAction(MTL::LoadActionClear);
  color->setStoreAction(MTL::StoreActionStore);
  color->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));

  MTL::RenderCommandEncoder *encoder =
      command->renderCommandEncoder(pass.get());
  if (encoder == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_encoder_failed",
                                 kPhase,
                                 "Cannot create a render command encoder"});
  }
  encoder->setRenderPipelineState(impl_->pipeline.get());
  encoder->setFragmentBytes(&frame_index, sizeof(frame_index), 0);
  encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{0},
                          NS::UInteger{3});
  encoder->endEncoding();
  command->commit();

  auto lease_impl = std::unique_ptr<SharedSurfaceLease::Impl>(
      new (std::nothrow) SharedSurfaceLease::Impl{});
  if (!lease_impl) {
    command->waitUntilCompleted();
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory", kPhase,
                                 "Cannot allocate shared-surface lease"});
  }
  lease_impl->command = std::move(command);
  lease_impl->cv_texture = std::move(cv_texture);
  lease_impl->buffer = std::move(*buffer);
  lease_impl->cache = impl_->texture_cache.get();
  lease_impl->frame = frame_index;
  lease_impl->state = SharedSurfaceLease::State::in_flight;
  return SharedSurfaceLease{std::move(lease_impl)};
}

std::uint64_t
nearest_rank_latency_ns(std::span<const std::uint64_t> sorted_samples,
                        double quantile) noexcept {
  if (sorted_samples.empty() || !(quantile >= 0.0) || !(quantile <= 1.0)) {
    return 0;
  }
  if (sorted_samples.size() == 1U) {
    return sorted_samples.front();
  }
  const auto rank = static_cast<std::size_t>(
      std::max<double>(1.0, std::ceil(quantile * static_cast<double>(
                                                     sorted_samples.size()))) -
      1.0);
  return sorted_samples[std::min(rank, sorted_samples.size() - 1U)];
}

} // namespace instantee::spike::transfer
