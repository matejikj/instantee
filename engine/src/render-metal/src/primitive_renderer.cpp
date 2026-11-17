#include <instantee/render_metal/primitive_renderer.hpp>

#include <instantee/render_core/renderer.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"
#include "metal_pipeline_payload.hpp"
#include "primitives_metallib.hpp"
#include "readback_impl.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <span>
#include <utility>

namespace instantee::render_metal {
namespace {

struct PrimitiveUniforms {
  float canvas[2];
  float pad0[2]{};
  float rect[4];
  float color[4];
  float line_end[2];
  float corner_radius = 0;
  float stroke_width = 0;
  std::uint32_t kind = 0;
  std::uint32_t pad1 = 0;
};
static_assert(sizeof(PrimitiveUniforms) == 72, "must match MSL layout");

[[nodiscard]] const char *ns_error_message(NS::Error *error) {
  if (error == nullptr || error->localizedDescription() == nullptr) {
    return "unknown Metal error";
  }
  const char *utf8 = error->localizedDescription()->utf8String();
  return utf8 == nullptr ? "unknown Metal error" : utf8;
}

[[nodiscard]] std::size_t align_up(std::size_t value, std::size_t alignment) {
  return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] render_core::PipelineVariantKey primitives_pipeline_key() {
  const auto bytes = std::span<const std::uint8_t>{
      detail::kPrimitivesMetallib, detail::kPrimitivesMetallib_size};
  return render_core::PipelineVariantKey{
      .feature_name = "solid-fill",
      .feature_version = 1,
      .pass_key = "primitives",
      .module_id = "primitives",
      .module_version = 1,
      .metallib_fingerprint = render_core::fingerprint_metallib_bytes(bytes),
      .color_pixel_format =
          static_cast<std::uint32_t>(MTL::PixelFormatRGBA8Unorm),
      .blending_enabled = true,
  };
}

[[nodiscard]] std::expected<detail::MetalOwner<MTL::RenderPipelineState>, Error>
compile_primitives_pipeline(MTL::Device *device) {
  NS::Error *error = nullptr;
  dispatch_data_t data = dispatch_data_create(
      detail::kPrimitivesMetallib, detail::kPrimitivesMetallib_size, nullptr,
      DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  if (data == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "metallib_dispatch_failed", "metal",
                                 "Cannot wrap primitives metallib"});
  }
  detail::MetalOwner<MTL::Library> library{device->newLibrary(data, &error)};
  dispatch_release(data);
  if (!library) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure, "metallib_load_failed",
                                 "metal", "Cannot load primitives metallib"});
  }
  detail::MetalOwner<MTL::Function> vertex{
      library->newFunction(NS::String::string(
          "vs_prim_main", NS::StringEncoding::UTF8StringEncoding))};
  detail::MetalOwner<MTL::Function> fragment{
      library->newFunction(NS::String::string(
          "fs_prim_main", NS::StringEncoding::UTF8StringEncoding))};
  if (!vertex || !fragment) {
    return std::unexpected(Error{ExitCode::gpu_failure, "shader_entry_missing",
                                 "metal",
                                 "Primitives shader entry points are missing"});
  }

  detail::MetalOwner<MTL::RenderPipelineDescriptor> desc{
      MTL::RenderPipelineDescriptor::alloc()->init()};
  if (!desc) {
    return std::unexpected(Error{ExitCode::gpu_failure, "pipeline_alloc_failed",
                                 "metal",
                                 "Cannot allocate primitives pipeline desc"});
  }
  desc->setVertexFunction(vertex.get());
  desc->setFragmentFunction(fragment.get());
  auto *attachment = desc->colorAttachments()->object(0);
  attachment->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  attachment->setBlendingEnabled(true);
  attachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
  attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
  attachment->setDestinationAlphaBlendFactor(
      MTL::BlendFactorOneMinusSourceAlpha);

  error = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline{
      device->newRenderPipelineState(desc.get(), &error)};
  if (!pipeline) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_create_failed", "metal",
                                 "Cannot create primitives pipeline"});
  }
  return pipeline;
}

[[nodiscard]] PrimitiveUniforms make_uniforms(const render_core::DrawOp &op,
                                              std::uint32_t width,
                                              std::uint32_t height) {
  PrimitiveUniforms u{};
  u.canvas[0] = static_cast<float>(width);
  u.canvas[1] = static_cast<float>(height);
  u.rect[0] = op.rect.x;
  u.rect[1] = op.rect.y;
  u.rect[2] = op.rect.width;
  u.rect[3] = op.rect.height;
  u.color[0] = static_cast<float>(op.color.r) / 255.0f;
  u.color[1] = static_cast<float>(op.color.g) / 255.0f;
  u.color[2] = static_cast<float>(op.color.b) / 255.0f;
  u.color[3] = static_cast<float>(op.color.a) / 255.0f;
  u.line_end[0] = op.x1;
  u.line_end[1] = op.y1;
  u.corner_radius = op.corner_radius;
  u.stroke_width = op.stroke_width;
  u.kind = static_cast<std::uint32_t>(op.kind);
  return u;
}

} // namespace

struct PrimitiveRenderer::Impl {
  MTL::Device *device = nullptr;
  MTL::CommandQueue *queue = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline;
};

PrimitiveRenderer::PrimitiveRenderer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
PrimitiveRenderer::PrimitiveRenderer(PrimitiveRenderer &&) noexcept = default;
PrimitiveRenderer &
PrimitiveRenderer::operator=(PrimitiveRenderer &&) noexcept = default;
PrimitiveRenderer::~PrimitiveRenderer() = default;

std::expected<PrimitiveRenderer, Error>
PrimitiveRenderer::create(Device &device,
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

  render_core::PipelineCache local;
  render_core::PipelineCache &cache =
      pipeline_cache != nullptr ? *pipeline_cache : local;
  auto cached = cache.get_or_compile(primitives_pipeline_key(), [&]() {
    auto compiled = compile_primitives_pipeline(native.device);
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
                                 "Primitives pipeline payload is invalid"});
  }

  auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::gpu_failure, "out_of_memory",
                                 "metal",
                                 "Cannot allocate primitive renderer"});
  }
  impl->device = native.device;
  impl->queue = native.queue;
  impl->pipeline = detail::retain_owner(payload->pipeline.get());
  return PrimitiveRenderer{std::move(impl)};
}

std::expected<Rgba8Frame, Error>
PrimitiveRenderer::render(const render_core::PassDesc &pass) {
  auto valid = render_core::validate_pass(pass);
  if (!valid) {
    return std::unexpected(valid.error());
  }
  if (!impl_ || impl_->device == nullptr || impl_->queue == nullptr ||
      !impl_->pipeline) {
    return std::unexpected(Error{ExitCode::gpu_failure, "renderer_not_ready",
                                 "metal", "Primitive renderer is not ready"});
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", "metal",
                                 "Cannot create Metal autorelease pool"});
  }

  detail::MetalOwner<MTL::TextureDescriptor> texture_desc{
      MTL::TextureDescriptor::alloc()->init()};
  if (!texture_desc) {
    return std::unexpected(
        Error{ExitCode::gpu_failure, "texture_alloc_failed", "metal",
              "Cannot allocate primitive target descriptor"});
  }
  texture_desc->setTextureType(MTL::TextureType2D);
  texture_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  texture_desc->setWidth(pass.width_px);
  texture_desc->setHeight(pass.height_px);
  texture_desc->setStorageMode(MTL::StorageModePrivate);
  texture_desc->setUsage(MTL::TextureUsageRenderTarget |
                         MTL::TextureUsageShaderRead);
  detail::MetalOwner<MTL::Texture> target{
      impl_->device->newTexture(texture_desc.get())};
  if (!target) {
    return std::unexpected(Error{ExitCode::gpu_failure, "texture_create_failed",
                                 "metal",
                                 "Cannot create primitive render target"});
  }

  const std::size_t alignment = std::max<std::size_t>(
      1U, impl_->device->minimumLinearTextureAlignmentForPixelFormat(
              MTL::PixelFormatRGBA8Unorm));
  const std::size_t bytes_per_row =
      align_up(static_cast<std::size_t>(pass.width_px) * 4U, alignment);
  const std::size_t staging_bytes =
      bytes_per_row * static_cast<std::size_t>(pass.height_px);
  detail::MetalOwner<MTL::Buffer> staging{
      impl_->device->newBuffer(staging_bytes, MTL::ResourceStorageModeShared)};
  if (!staging) {
    return std::unexpected(Error{ExitCode::gpu_failure, "staging_alloc_failed",
                                 "metal",
                                 "Cannot allocate primitive staging buffer"});
  }

  MTL::CommandBuffer *raw_command = impl_->queue->commandBuffer();
  if (raw_command == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 "metal",
                                 "Cannot create a Metal command buffer"});
  }
  detail::MetalOwner<MTL::CommandBuffer> command =
      detail::retain_owner(raw_command);

  render_core::Rgba8 clear_color{0, 0, 0, 255};
  for (const render_core::DrawOp &op : pass.ops) {
    if (op.kind == render_core::DrawOpKind::clear) {
      clear_color = op.color;
      break;
    }
  }

  detail::MetalOwner<MTL::RenderPassDescriptor> rpd{
      MTL::RenderPassDescriptor::alloc()->init()};
  if (!rpd) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_pass_failed",
                                 "metal",
                                 "Cannot allocate render pass descriptor"});
  }
  auto *color = rpd->colorAttachments()->object(0);
  color->setTexture(target.get());
  color->setLoadAction(MTL::LoadActionClear);
  color->setStoreAction(MTL::StoreActionStore);
  color->setClearColor(
      MTL::ClearColor::Make(clear_color.r / 255.0, clear_color.g / 255.0,
                            clear_color.b / 255.0, clear_color.a / 255.0));

  MTL::RenderCommandEncoder *encoder = command->renderCommandEncoder(rpd.get());
  if (encoder == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_encoder_failed",
                                 "metal",
                                 "Cannot create a render command encoder"});
  }
  encoder->setRenderPipelineState(impl_->pipeline.get());
  for (const render_core::DrawOp &op : pass.ops) {
    if (op.kind == render_core::DrawOpKind::clear) {
      continue;
    }
    const PrimitiveUniforms uniforms =
        make_uniforms(op, pass.width_px, pass.height_px);
    encoder->setVertexBytes(&uniforms, sizeof(uniforms), 0);
    encoder->setFragmentBytes(&uniforms, sizeof(uniforms), 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{0},
                            NS::UInteger{3});
  }
  encoder->endEncoding();

  MTL::BlitCommandEncoder *blit = command->blitCommandEncoder();
  if (blit == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "blit_encoder_failed",
                                 "metal",
                                 "Cannot create a blit command encoder"});
  }
  blit->copyFromTexture(target.get(), 0, 0, MTL::Origin(0, 0, 0),
                        MTL::Size(pass.width_px, pass.height_px, 1),
                        staging.get(), 0, bytes_per_row,
                        bytes_per_row * pass.height_px);
  blit->endEncoding();
  command->commit();
  command->waitUntilCompleted();
  if (command->status() == MTL::CommandBufferStatusError) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 "metal", "Primitive render command failed"});
  }

  Rgba8Frame frame;
  frame.extent.width = pass.width_px;
  frame.extent.height = pass.height_px;
  frame.rgba.resize(static_cast<std::size_t>(pass.width_px) *
                    static_cast<std::size_t>(pass.height_px) * 4U);
  const auto *src = static_cast<const std::uint8_t *>(staging->contents());
  for (std::uint32_t y = 0; y < pass.height_px; ++y) {
    std::memcpy(frame.rgba.data() +
                    static_cast<std::size_t>(y) * pass.width_px * 4U,
                src + static_cast<std::size_t>(y) * bytes_per_row,
                static_cast<std::size_t>(pass.width_px) * 4U);
  }
  return frame;
}

} // namespace instantee::render_metal
