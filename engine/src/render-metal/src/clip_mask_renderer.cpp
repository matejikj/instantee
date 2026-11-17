#include <instantee/render_metal/clip_mask_renderer.hpp>

#include <instantee/render_core/instance_batch.hpp>
#include <instantee/render_core/renderer.hpp>

#include "device_native.hpp"
#include "metal_owner.hpp"
#include "metal_pipeline_payload.hpp"
#include "primitives_metallib.hpp"

#include <Metal/Metal.hpp>

#include <algorithm>
#include <cstring>
#include <new>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace instantee::render_metal {
namespace {

struct CanvasUniforms {
  float canvas[2];
  float pad0[2]{};
};
static_assert(sizeof(CanvasUniforms) == 16, "must match MSL layout");

struct InstanceGpu {
  float rect[4];
  float color[4];
  float line_end[2];
  float corner_radius = 0;
  float stroke_width = 0;
  std::uint32_t kind = 0;
  std::uint32_t pad1 = 0;
  float pad2[2]{};
};
static_assert(sizeof(InstanceGpu) == 64, "must match MSL InstanceData stride");

struct ScissorLayer {
  render_core::PixelScissor scissor{};
};
struct MaskLayer {
  render_core::DrawOp shape{};
};
using StackLayer = std::variant<ScissorLayer, MaskLayer>;

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

[[nodiscard]] InstanceGpu make_instance(const render_core::DrawOp &op) {
  InstanceGpu u{};
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

[[nodiscard]] render_core::PixelScissor
effective_scissor(const std::vector<StackLayer> &stack, std::uint32_t width_px,
                  std::uint32_t height_px) {
  render_core::PixelScissor current{
      .x = 0,
      .y = 0,
      .width = static_cast<std::int32_t>(width_px),
      .height = static_cast<std::int32_t>(height_px)};
  for (const auto &layer : stack) {
    if (const auto *scissor = std::get_if<ScissorLayer>(&layer)) {
      current = render_core::intersect_scissors(current, scissor->scissor);
      if (render_core::is_empty_scissor(current)) {
        return current;
      }
    }
  }
  return current;
}

[[nodiscard]] std::uint32_t mask_depth(const std::vector<StackLayer> &stack) {
  std::uint32_t depth = 0;
  for (const auto &layer : stack) {
    if (std::holds_alternative<MaskLayer>(layer)) {
      ++depth;
    }
  }
  return depth;
}

[[nodiscard]] std::expected<detail::MetalOwner<MTL::RenderPipelineState>, Error>
compile_clip_pipeline(MTL::Device *device, bool write_color) {
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
                                 "Cannot allocate clip pipeline descriptor"});
  }
  desc->setVertexFunction(vertex.get());
  desc->setFragmentFunction(fragment.get());
  desc->setStencilAttachmentPixelFormat(MTL::PixelFormatStencil8);
  auto *attachment = desc->colorAttachments()->object(0);
  attachment->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  attachment->setWriteMask(write_color ? MTL::ColorWriteMaskAll
                                       : MTL::ColorWriteMaskNone);
  attachment->setBlendingEnabled(write_color);
  if (write_color) {
    attachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    attachment->setDestinationRGBBlendFactor(
        MTL::BlendFactorOneMinusSourceAlpha);
    attachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    attachment->setDestinationAlphaBlendFactor(
        MTL::BlendFactorOneMinusSourceAlpha);
  }

  error = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline{
      device->newRenderPipelineState(desc.get(), &error)};
  if (!pipeline) {
    static_cast<void>(ns_error_message(error));
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "pipeline_create_failed", "metal",
                                 "Cannot create clip/mask pipeline"});
  }
  return pipeline;
}

[[nodiscard]] detail::MetalOwner<MTL::DepthStencilState>
make_stencil_state(MTL::Device *device, MTL::CompareFunction compare,
                   MTL::StencilOperation on_pass) {
  detail::MetalOwner<MTL::StencilDescriptor> face{
      MTL::StencilDescriptor::alloc()->init()};
  if (!face) {
    return {};
  }
  face->setStencilCompareFunction(compare);
  face->setStencilFailureOperation(MTL::StencilOperationKeep);
  face->setDepthFailureOperation(MTL::StencilOperationKeep);
  face->setDepthStencilPassOperation(on_pass);
  face->setReadMask(0xFF);
  face->setWriteMask(0xFF);

  detail::MetalOwner<MTL::DepthStencilDescriptor> desc{
      MTL::DepthStencilDescriptor::alloc()->init()};
  if (!desc) {
    return {};
  }
  desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
  desc->setDepthWriteEnabled(false);
  desc->setFrontFaceStencil(face.get());
  desc->setBackFaceStencil(face.get());
  return detail::MetalOwner<MTL::DepthStencilState>{
      device->newDepthStencilState(desc.get())};
}

} // namespace

struct ClipMaskRenderer::Impl {
  MTL::Device *device = nullptr;
  MTL::CommandQueue *queue = nullptr;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline_color;
  detail::MetalOwner<MTL::RenderPipelineState> pipeline_stencil_only;
  detail::MetalOwner<MTL::DepthStencilState> stencil_incr;
  detail::MetalOwner<MTL::DepthStencilState> stencil_decr;
  detail::MetalOwner<MTL::DepthStencilState> stencil_equal;
  detail::MetalOwner<MTL::DepthStencilState> stencil_disabled;
};

ClipMaskRenderer::ClipMaskRenderer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
ClipMaskRenderer::ClipMaskRenderer(ClipMaskRenderer &&) noexcept = default;
ClipMaskRenderer &
ClipMaskRenderer::operator=(ClipMaskRenderer &&) noexcept = default;
ClipMaskRenderer::~ClipMaskRenderer() = default;

std::expected<ClipMaskRenderer, Error>
ClipMaskRenderer::create(Device &device,
                         render_core::PipelineCache * /*pipeline_cache*/) {
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

  auto pipeline_color = compile_clip_pipeline(native.device, true);
  if (!pipeline_color) {
    return std::unexpected(pipeline_color.error());
  }
  auto pipeline_stencil = compile_clip_pipeline(native.device, false);
  if (!pipeline_stencil) {
    return std::unexpected(pipeline_stencil.error());
  }

  auto impl = std::unique_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::out_of_memory, "out_of_memory",
                                 "metal",
                                 "Cannot allocate clip mask renderer"});
  }
  impl->device = native.device;
  impl->queue = native.queue;
  impl->pipeline_color = std::move(*pipeline_color);
  impl->pipeline_stencil_only = std::move(*pipeline_stencil);
  impl->stencil_incr =
      make_stencil_state(native.device, MTL::CompareFunctionEqual,
                         MTL::StencilOperationIncrementClamp);
  impl->stencil_decr =
      make_stencil_state(native.device, MTL::CompareFunctionEqual,
                         MTL::StencilOperationDecrementClamp);
  impl->stencil_equal = make_stencil_state(
      native.device, MTL::CompareFunctionEqual, MTL::StencilOperationKeep);
  impl->stencil_disabled = make_stencil_state(
      native.device, MTL::CompareFunctionAlways, MTL::StencilOperationKeep);
  if (!impl->stencil_incr || !impl->stencil_decr || !impl->stencil_equal ||
      !impl->stencil_disabled) {
    return std::unexpected(Error{ExitCode::gpu_failure, "stencil_state_failed",
                                 "metal",
                                 "Cannot create stencil depth states"});
  }
  // Disabled state must not write stencil.
  {
    detail::MetalOwner<MTL::StencilDescriptor> face{
        MTL::StencilDescriptor::alloc()->init()};
    face->setStencilCompareFunction(MTL::CompareFunctionAlways);
    face->setStencilFailureOperation(MTL::StencilOperationKeep);
    face->setDepthFailureOperation(MTL::StencilOperationKeep);
    face->setDepthStencilPassOperation(MTL::StencilOperationKeep);
    face->setReadMask(0xFF);
    face->setWriteMask(0x00);
    detail::MetalOwner<MTL::DepthStencilDescriptor> desc{
        MTL::DepthStencilDescriptor::alloc()->init()};
    desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
    desc->setDepthWriteEnabled(false);
    desc->setFrontFaceStencil(face.get());
    desc->setBackFaceStencil(face.get());
    impl->stencil_disabled.reset(
        native.device->newDepthStencilState(desc.get()));
    if (!impl->stencil_disabled) {
      return std::unexpected(Error{ExitCode::gpu_failure,
                                   "stencil_state_failed", "metal",
                                   "Cannot create disabled stencil state"});
    }
  }
  return ClipMaskRenderer{std::move(impl)};
}

std::expected<Rgba8Frame, Error>
ClipMaskRenderer::render(const render_core::ClipScript &script) {
  if (auto valid = render_core::validate_clip_script(script); !valid) {
    return std::unexpected(valid.error());
  }
  if (!impl_ || impl_->device == nullptr || impl_->queue == nullptr ||
      !impl_->pipeline_color || !impl_->pipeline_stencil_only) {
    return std::unexpected(Error{ExitCode::gpu_failure, "renderer_not_ready",
                                 "metal", "Clip mask renderer is not ready"});
  }

  detail::MetalOwner<NS::AutoreleasePool> pool{
      NS::AutoreleasePool::alloc()->init()};
  if (!pool) {
    return std::unexpected(Error{ExitCode::gpu_failure,
                                 "autorelease_pool_failed", "metal",
                                 "Cannot create Metal autorelease pool"});
  }

  detail::MetalOwner<MTL::TextureDescriptor> color_desc{
      MTL::TextureDescriptor::alloc()->init()};
  color_desc->setTextureType(MTL::TextureType2D);
  color_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  color_desc->setWidth(script.width_px);
  color_desc->setHeight(script.height_px);
  color_desc->setStorageMode(MTL::StorageModePrivate);
  color_desc->setUsage(MTL::TextureUsageRenderTarget |
                       MTL::TextureUsageShaderRead);
  detail::MetalOwner<MTL::Texture> color{
      impl_->device->newTexture(color_desc.get())};
  if (!color) {
    return std::unexpected(Error{ExitCode::gpu_failure, "texture_create_failed",
                                 "metal", "Cannot create clip color target"});
  }

  detail::MetalOwner<MTL::TextureDescriptor> stencil_desc{
      MTL::TextureDescriptor::alloc()->init()};
  stencil_desc->setTextureType(MTL::TextureType2D);
  stencil_desc->setPixelFormat(MTL::PixelFormatStencil8);
  stencil_desc->setWidth(script.width_px);
  stencil_desc->setHeight(script.height_px);
  stencil_desc->setStorageMode(MTL::StorageModePrivate);
  stencil_desc->setUsage(MTL::TextureUsageRenderTarget);
  detail::MetalOwner<MTL::Texture> stencil{
      impl_->device->newTexture(stencil_desc.get())};
  if (!stencil) {
    return std::unexpected(Error{ExitCode::gpu_failure, "stencil_create_failed",
                                 "metal", "Cannot create clip stencil target"});
  }

  const std::size_t alignment = std::max<std::size_t>(
      1U, impl_->device->minimumLinearTextureAlignmentForPixelFormat(
              MTL::PixelFormatRGBA8Unorm));
  const std::size_t bytes_per_row =
      align_up(static_cast<std::size_t>(script.width_px) * 4U, alignment);
  const std::size_t staging_bytes =
      bytes_per_row * static_cast<std::size_t>(script.height_px);
  detail::MetalOwner<MTL::Buffer> staging{
      impl_->device->newBuffer(staging_bytes, MTL::ResourceStorageModeShared)};
  if (!staging) {
    return std::unexpected(Error{ExitCode::gpu_failure, "staging_alloc_failed",
                                 "metal",
                                 "Cannot allocate clip staging buffer"});
  }

  MTL::CommandBuffer *raw_command = impl_->queue->commandBuffer();
  if (raw_command == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 "metal",
                                 "Cannot create a Metal command buffer"});
  }
  detail::MetalOwner<MTL::CommandBuffer> command =
      detail::retain_owner(raw_command);

  detail::MetalOwner<MTL::RenderPassDescriptor> rpd{
      MTL::RenderPassDescriptor::alloc()->init()};
  auto *color_att = rpd->colorAttachments()->object(0);
  color_att->setTexture(color.get());
  color_att->setLoadAction(MTL::LoadActionClear);
  color_att->setStoreAction(MTL::StoreActionStore);
  color_att->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  auto *stencil_att = rpd->stencilAttachment();
  stencil_att->setTexture(stencil.get());
  stencil_att->setLoadAction(MTL::LoadActionClear);
  stencil_att->setStoreAction(MTL::StoreActionDontCare);
  stencil_att->setClearStencil(0);

  MTL::RenderCommandEncoder *encoder = command->renderCommandEncoder(rpd.get());
  if (encoder == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "render_encoder_failed",
                                 "metal", "Cannot create clip render encoder"});
  }
  CanvasUniforms canvas{};
  canvas.canvas[0] = static_cast<float>(script.width_px);
  canvas.canvas[1] = static_cast<float>(script.height_px);
  encoder->setFragmentBytes(&canvas, sizeof(canvas), 0);

  std::vector<detail::MetalOwner<MTL::Buffer>> live_buffers;
  live_buffers.reserve(script.commands.size());
  std::vector<StackLayer> stack;
  stack.reserve(render_core::ClipScript::kMaxDepth);

  auto draw_op = [&](const render_core::DrawOp &op,
                     bool write_color) -> std::expected<void, Error> {
    InstanceGpu gpu = make_instance(op);
    if (op.kind == render_core::DrawOpKind::clear) {
      // Replace the whole target with an opaque rect when stack is empty.
      gpu.kind = static_cast<std::uint32_t>(render_core::DrawOpKind::fill_rect);
      gpu.rect[0] = 0;
      gpu.rect[1] = 0;
      gpu.rect[2] = static_cast<float>(script.width_px);
      gpu.rect[3] = static_cast<float>(script.height_px);
      gpu.color[3] = 1.0f;
    }
    detail::MetalOwner<MTL::Buffer> buffer{impl_->device->newBuffer(
        &gpu, sizeof(gpu), MTL::ResourceStorageModeShared)};
    if (!buffer) {
      return std::unexpected(Error{ExitCode::gpu_failure,
                                   "instance_buffer_failed", "metal",
                                   "Cannot allocate clip instance buffer"});
    }
    const auto scissor =
        effective_scissor(stack, script.width_px, script.height_px);
    if (render_core::is_empty_scissor(scissor) && write_color &&
        op.kind != render_core::DrawOpKind::clear) {
      return {};
    }
    if (!render_core::is_empty_scissor(scissor)) {
      encoder->setScissorRect(
          MTL::ScissorRect{static_cast<NS::UInteger>(scissor.x),
                           static_cast<NS::UInteger>(scissor.y),
                           static_cast<NS::UInteger>(scissor.width),
                           static_cast<NS::UInteger>(scissor.height)});
    } else {
      encoder->setScissorRect(
          MTL::ScissorRect{0, 0, script.width_px, script.height_px});
    }
    encoder->setRenderPipelineState(write_color
                                        ? impl_->pipeline_color.get()
                                        : impl_->pipeline_stencil_only.get());
    encoder->setFragmentBuffer(buffer.get(), 0, 1);
    encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger{0},
                            NS::UInteger{3}, NS::UInteger{1});
    live_buffers.push_back(std::move(buffer));
    return {};
  };

  for (const auto &cmd : script.commands) {
    using render_core::ClipCommandKind;
    switch (cmd.kind) {
    case ClipCommandKind::push_scissor:
      stack.push_back(
          ScissorLayer{.scissor = render_core::scissor_from_rect(
                           cmd.scissor, script.width_px, script.height_px)});
      break;
    case ClipCommandKind::push_shape_mask: {
      const std::uint32_t depth = mask_depth(stack);
      encoder->setDepthStencilState(impl_->stencil_incr.get());
      encoder->setStencilReferenceValue(depth);
      if (auto drawn = draw_op(cmd.op, /*write_color=*/false); !drawn) {
        encoder->endEncoding();
        return std::unexpected(drawn.error());
      }
      stack.push_back(MaskLayer{.shape = cmd.op});
      break;
    }
    case ClipCommandKind::pop: {
      if (stack.empty()) {
        encoder->endEncoding();
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "clip_stack_underflow", "metal",
                                     "pop without matching push"});
      }
      if (std::holds_alternative<MaskLayer>(stack.back())) {
        const auto shape = std::get<MaskLayer>(stack.back()).shape;
        const std::uint32_t depth = mask_depth(stack);
        encoder->setDepthStencilState(impl_->stencil_decr.get());
        encoder->setStencilReferenceValue(depth);
        if (auto drawn = draw_op(shape, /*write_color=*/false); !drawn) {
          encoder->endEncoding();
          return std::unexpected(drawn.error());
        }
      }
      stack.pop_back();
      break;
    }
    case ClipCommandKind::draw: {
      const std::uint32_t depth = mask_depth(stack);
      if (depth == 0) {
        encoder->setDepthStencilState(impl_->stencil_disabled.get());
      } else {
        encoder->setDepthStencilState(impl_->stencil_equal.get());
        encoder->setStencilReferenceValue(depth);
      }
      if (auto drawn = draw_op(cmd.op, /*write_color=*/true); !drawn) {
        encoder->endEncoding();
        return std::unexpected(drawn.error());
      }
      break;
    }
    }
  }
  encoder->endEncoding();

  MTL::BlitCommandEncoder *blit = command->blitCommandEncoder();
  if (blit == nullptr) {
    return std::unexpected(Error{ExitCode::gpu_failure, "blit_encoder_failed",
                                 "metal",
                                 "Cannot create a blit command encoder"});
  }
  blit->copyFromTexture(color.get(), 0, 0, MTL::Origin(0, 0, 0),
                        MTL::Size(script.width_px, script.height_px, 1),
                        staging.get(), 0, bytes_per_row,
                        bytes_per_row * script.height_px);
  blit->endEncoding();
  command->commit();
  command->waitUntilCompleted();
  if (command->status() == MTL::CommandBufferStatusError) {
    return std::unexpected(Error{ExitCode::gpu_failure, "command_buffer_failed",
                                 "metal", "Clip/mask render command failed"});
  }

  Rgba8Frame frame;
  frame.extent.width = script.width_px;
  frame.extent.height = script.height_px;
  frame.rgba.resize(static_cast<std::size_t>(script.width_px) *
                    static_cast<std::size_t>(script.height_px) * 4U);
  const auto *src = static_cast<const std::uint8_t *>(staging->contents());
  for (std::uint32_t y = 0; y < script.height_px; ++y) {
    std::memcpy(frame.rgba.data() +
                    static_cast<std::size_t>(y) * script.width_px * 4U,
                src + static_cast<std::size_t>(y) * bytes_per_row,
                static_cast<std::size_t>(script.width_px) * 4U);
  }
  return frame;
}

} // namespace instantee::render_metal
