#include <instantee/render_metal/test_pattern.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"
#include "readback_impl.hpp"
#include "test_pattern_source.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <new>
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
TestPatternRenderer::create(Device &device) {
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

  NS::Error *error = nullptr;
  // string() returns an autoreleased object kept alive by the local pool.
  NS::String *source = NS::String::string(
      detail::kTestPatternSource, NS::StringEncoding::UTF8StringEncoding);
  if (source == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "shader_source_failed",
                                 "metal",
                                 "Cannot allocate shader source string"});
  }
  // newLibrary returns +1 on success.
  detail::MetalOwner<MTL::Library> library{
      native.device->newLibrary(source, nullptr, &error)};
  if (!library) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure, "shader_compile_failed",
                                 "metal", "Cannot compile test-pattern MSL"});
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
      native.device->newRenderPipelineState(pipeline_desc.get(), &error)};
  if (!pipeline) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_create_failed", "metal",
                                 "Cannot create test-pattern render pipeline"});
  }

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
