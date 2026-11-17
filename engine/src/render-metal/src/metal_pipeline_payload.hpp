#pragma once

#include <instantee/render_core/pipeline_cache.hpp>

#include "metal_owner.hpp"

#include <Metal/Metal.hpp>

namespace instantee::render_metal::detail {

// Owns one MTLRenderPipelineState inside the platform-neutral PipelineCache.
struct MetalPipelinePayload final : render_core::PipelinePayload {
  explicit MetalPipelinePayload(MetalOwner<MTL::RenderPipelineState> state)
      : pipeline(std::move(state)) {}

  MetalOwner<MTL::RenderPipelineState> pipeline;
};

} // namespace instantee::render_metal::detail
