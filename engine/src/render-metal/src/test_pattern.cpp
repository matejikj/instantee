#include <instantee/render_metal/test_pattern.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"
#include "metal_pipeline_payload.hpp"
#include "readback_impl.hpp"
#include "test_pattern_metallib.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <new>
#include <span>
#include <utility>

namespace instantee::render_metal {
namespace {

[[nodiscard]] std::size_t align_up(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] const char *ns_error_message(NS::Error *error) {
  if (error == nullptr || error->localizedDescription() == nullptr) {
    return "unknown Metal error";
  }
  const char *utf8 = error->localizedDescription()->utf8String();
  return utf8 == nullptr ? "unknown Metal error" : utf8;
}

[[nodiscard]] std::expected<detail::MetalOwner<MTL::RenderPipelineState>, Error>
compile_test_pattern_pipeline(MTL::Device *device) {
  NS::Error *error = nullptr;
  dispatch_data_t metallib_data = dispatch_data_create(
      detail::kTestPatternMetallib, detail::kTestPatternMetallib_size, nullptr,
      DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  if (metallib_data == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metallib_dispatch_failed", "metal",
                                 "Cannot wrap offline test-pattern metallib"});
  }
  detail::MetalOwner<MTL::Library> library{
      device->newLibrary(metallib_data, &error)};
  dispatch_release(metallib_data);
  if (!library) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure, "metallib_load_failed",
                                 "metal",
                                 "Cannot load offline test-pattern metallib"});
  }

  detail::MetalOwner<MTL::Function> vertex{library->newFunction(
      NS::String::string("vs_main", NS::StringEncoding::UTF8StringEncoding))};
  detail::MetalOwner<MTL::Function> fragment{library->newFunction(
      NS::String::string("fs_main", NS::StringEncoding::UTF8StringEncoding))};
  if (!vertex || !fragment) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "shader_entry_missing", "metal",
              "Test-pattern shader entry points are missing"});
  }

  detail::MetalOwner<MTL::RenderPipelineDescriptor> pipeline_desc{
      MTL::RenderPipelineDescriptor::alloc()->init()};
  if (!pipeline_desc) {
    return std::unexpected(Error{ExitCode::gpu_failure, "pipeline_alloc_failed",
                                 "metal",
                                 "Cannot allocate render pipeline descriptor"});
  }
  pipeline_desc->setVertexFunction(vertex.get());
  pipeline_desc->setFragmentFunction(fragment.get());
  auto *attachment = pipeline_desc->colorAttachments()->object(0);
  attachment->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  attachment->setBlendingEnabled(false);

  error = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline{
      device->newRenderPipelineState(pipeline_desc.get(), &error)};
  if (!pipeline) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_create_failed", "metal",
                                 "Cannot create test-pattern render pipeline"});
  }
  return pipeline;
}

[[nodiscard]] render_core::PipelineVariantKey test_pattern_pipeline_key() {
  const auto bytes = std::span<const std::uint8_t>{
      detail::kTestPatternMetallib, detail::kTestPatternMetallib_size};
  return render_core::PipelineVariantKey{
      .feature_name = "test_pattern",
      .feature_version = 1,
      .pass_key = "main_color",
      .module_id = "test_pattern",
      .module_version = 1,
      .metallib_fingerprint = render_core::fingerprint_metallib_bytes(bytes),
      .color_pixel_format =
          static_cast<std::uint32_t>(MTL::PixelFormatRGBA8Unorm),
      .blending_enabled = false,
  };
}

} // namespace

struct TestPatternRenderer::Impl {
  MTL::Device *device = nullptr;
  MTL::CommandQueue *queue = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline;
  detail::MetalOwner<MTL::Texture> target;
  std::size_t bytes_per_row = 0;
};

TestPatternRenderer::TestPatternRenderer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

TestPatternRenderer::TestPatternRenderer(TestPatternRenderer &&) noexcept =
    default;
TestPatternRenderer &
TestPatternRenderer::operator=(TestPatternRenderer &&) noexcept = default;
TestPatternRenderer::~TestPatternRenderer() = default;

std::expected<TestPatternRenderer, Error>
TestPatternRenderer::create(Device &device,
                            render_core::PipelineCache *pipeline_cache) {
  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", "metal",
                                 "Cannot create Metal autorelease pool"});
  }

  const detail::DeviceNative native = detail::native_handles(device);
  if (native.device == nullptr || native.queue == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metal_device_unavailable", "metal",
                                 "Metal device handles are unavailable"});
  }

  render_core::PipelineCache local_cache;
  render_core::PipelineCache &cache =
      pipeline_cache != nullptr ? *pipeline_cache : local_cache;
  const render_core::PipelineVariantKey key = test_pattern_pipeline_key();
  auto cached = cache.get_or_compile(key, [&]() {
    auto compiled = compile_test_pattern_pipeline(native.device);
    if (!compiled) {
      return std::expected<std::unique_ptr<render_core::PipelinePayload>,
                           Error>{std::unexpected(compiled.error())};
    }
    return std::expected<std::unique_ptr<render_core::PipelinePayload>, Error>{
        std::make_unique<detail::MetalPipelinePayload>(std::move(*compiled))};
  });
  if (!cached) {
    return std::unexpected(cached.error());
  }
  auto *payload = dynamic_cast<detail::MetalPipelinePayload *>(*cached);
  if (payload == nullptr || !payload->pipeline) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_payload_invalid", "metal",
                                 "Cached pipeline payload is not a Metal PSO"});
  }
  // Retain into the renderer so lifetime does not depend on the cache entry.
  detail::MetalOwner<MTL::RenderPipelineState> pipeline =
      detail::retain_owner(payload->pipeline.get());

  detail::MetalOwner<MTL::TextureDescriptor> texture_desc{
      MTL::TextureDescriptor::alloc()->init()};
  if (!texture_desc) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "texture_alloc_failed", "metal",
              "Cannot allocate offscreen texture descriptor"});
  }
  texture_desc->setTextureType(MTL::TextureType2D);
  texture_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  texture_desc->setWidth(kTestPatternWidth);
  texture_desc->setHeight(kTestPatternHeight);
  texture_desc->setStorageMode(MTL::StorageModePrivate);
  texture_desc->setUsage(MTL::TextureUsageRenderTarget |
                         MTL::TextureUsageShaderRead);

  detail::MetalOwner<MTL::Texture> target{
      native.device->newTexture(texture_desc.get())};
  if (!target) {
    return std::unexpected(Error{ExitCode::gpu_failure, "texture_create_failed",
                                 "metal",
                                 "Cannot create 1080x1920 offscreen texture"});
  }

  const std::size_t alignment = std::max<std::size_t>(
      1U, native.device->minimumLinearTextureAlignmentForPixelFormat(
              MTL::PixelFormat::PixelFormatRGBA8Unorm));
  const std::size_t bytes_per_row = align_up(
      static_cast<std::size_t>(kTestPatternWidth) * kTestPatternChannels,
      alignment);

  auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory",
                                 "metal", "Cannot allocate renderer state"});
  }
  impl->device = native.device;
  impl->queue = native.queue;
  impl->pipeline = std::move(pipeline);
  impl->target = std::move(target);
  impl->bytes_per_row = bytes_per_row;
  return TestPatternRenderer{std::move(impl)};
}

std::expected<ReadbackLease, Error>
TestPatternRenderer::submit(std::uint32_t frame_index) {
  if (frame_index > 1U) {
    return std::unexpected(Error{ExitCode::invalid_input, "unsupported_frame",
                                 "metal",
                                 "Spike test pattern supports frames 0 and 1"});
  }
  if (!impl_ || impl_->device == nullptr || impl_->queue == nullptr ||
      !impl_->pipeline || !impl_->target) {
    return std::unexpected(Error{ExitCode::gpu_failure, "renderer_not_ready",
                                 "metal",
                                 "Test-pattern renderer is not ready"});
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", "metal",
                                 "Cannot create Metal autorelease pool"});
  }

  const std::size_t staging_bytes =
      impl_->bytes_per_row * static_cast<std::size_t>(kTestPatternHeight);
  if (staging_bytes >
      static_cast<std::size_t>(impl_->device->recommendedMaxWorkingSetSize())) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "staging_budget_exceeded", "metal",
              "Staging buffer exceeds recommended working set"});
  }

  // Each lease owns its staging buffer so delayed completion/cancel cannot
  // recycle a shared buffer while GPU work is still in flight.
  detail::MetalOwner<MTL::Buffer> staging{
      impl_->device->newBuffer(staging_bytes, MTL::ResourceStorageModeShared)};
  if (!staging) {
    return std::unexpected(Error{ExitCode::gpu_failure, "staging_alloc_failed",
                                 "metal",
                                 "Cannot allocate CPU-visible staging buffer"});
  }

  // commandBuffer() is autoreleased; retain it into the lease before the pool
  // drains so completion can outlive this stack frame.
  MTL::CommandBuffer *raw_command = impl_->queue->commandBuffer();
  if (raw_command == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 "metal",
                                 "Cannot create a Metal command buffer"});
  }
  detail::MetalOwner<MTL::CommandBuffer> command =
      detail::retain_owner(raw_command);

  detail::MetalOwner<MTL::RenderPassDescriptor> pass{
      MTL::RenderPassDescriptor::alloc()->init()};
  if (!pass) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_pass_failed",
                                 "metal",
                                 "Cannot allocate render pass descriptor"});
  }
  auto *color = pass->colorAttachments()->object(0);
  color->setTexture(impl_->target.get());
  color->setLoadAction(MTL::LoadActionClear);
  color->setStoreAction(MTL::StoreActionStore);
  color->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));

  MTL::RenderCommandEncoder *encoder =
      command->renderCommandEncoder(pass.get());
  if (encoder == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_encoder_failed",
                                 "metal",
                                 "Cannot create a render command encoder"});
  }
  encoder->setRenderPipelineState(impl_->pipeline.get());
  encoder->setFragmentBytes(&frame_index, sizeof(frame_index), 0);
  encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{0},
                          NS::UInteger{3});
  encoder->endEncoding();

  MTL::BlitCommandEncoder *blit = command->blitCommandEncoder();
  if (blit == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "blit_encoder_failed",
                                 "metal",
                                 "Cannot create a blit command encoder"});
  }
  blit->copyFromTexture(impl_->target.get(), 0, 0, MTL::Origin(0, 0, 0),
                        MTL::Size(kTestPatternWidth, kTestPatternHeight, 1),
                        staging.get(), 0, impl_->bytes_per_row,
                        impl_->bytes_per_row * kTestPatternHeight);
  blit->endEncoding();

  command->commit();

  auto lease_impl = std::unique_ptr<ReadbackLease::Impl>(
      new (std::nothrow) ReadbackLease::Impl{});
  if (!lease_impl) {
    // Keep GPU resources alive through retirement even on allocation failure.
    command->waitUntilCompleted();
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory",
                                 "metal", "Cannot allocate readback lease"});
  }
  lease_impl->command = std::move(command);
  lease_impl->staging = std::move(staging);
  lease_impl->target = detail::retain_owner(impl_->target.get());
  lease_impl->bytes_per_row = impl_->bytes_per_row;
  lease_impl->width = kTestPatternWidth;
  lease_impl->height = kTestPatternHeight;
  lease_impl->state = ReadbackLease::State::in_flight;
  return ReadbackLease{std::move(lease_impl)};
}

std::expected<Rgba8Frame, Error>
TestPatternRenderer::render(std::uint32_t frame_index) {
  auto lease = submit(frame_index);
  if (!lease) {
    return std::unexpected(lease.error());
  }
  return lease->take_pixels();
}

} // namespace instantee::render_metal
