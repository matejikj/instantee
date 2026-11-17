#pragma once

#include <instantee/diagnostics/error.hpp>
#include <instantee/render_core/draw_ops.hpp>
#include <instantee/render_core/renderer.hpp>

#include <cstdint>
#include <expected>
#include <vector>

namespace instantee::render_core {

// How a pass touches an opaque resource id. Handles stay generation-free here;
// the registry assigns real GPU objects later. Zero is never a valid resource.
enum class ResourceUsage : std::uint8_t {
  sample = 1,
  color_write = 2,
};

struct GraphResourceEdge {
  std::uint64_t resource = 0;
  ResourceUsage usage = ResourceUsage::sample;
};

// One node in the authoring graph. `pass.color_target` must appear as a
// color_write edge (added automatically when omitted). Sampled resources that
// are neither written earlier in the graph nor listed as imported are rejected.
struct GraphPassNode {
  PassDesc pass{};
  std::vector<GraphResourceEdge> resources{};
};

struct RenderGraphDesc {
  std::vector<GraphPassNode> passes{};
  // Resource that must be produced by a live pass after dead-pass elimination.
  std::uint64_t output_resource = 0;
  // Resources that may be sampled without a writer inside this graph.
  std::vector<std::uint64_t> imported_resources{};
};

enum class BarrierKind : std::uint8_t {
  color_write_to_sample = 1,
};

// Logical barrier between ordered passes. Backends may map these to GPU
// fences later; compile records them so usage transitions stay explicit.
struct ResourceBarrier {
  std::uint64_t resource = 0;
  BarrierKind kind = BarrierKind::color_write_to_sample;
  std::size_t after_order_index = 0;
  std::size_t before_order_index = 0;
};

struct CompiledRenderGraph {
  // Indices into the original `RenderGraphDesc::passes` after DCE, in submit
  // order. Empty when the graph has no live work (always a compile error).
  std::vector<std::size_t> pass_order{};
  std::vector<ResourceBarrier> barriers{};
  std::uint64_t output_resource = 0;
};

// CPU reference image for graph property tests and golden checks. RGBA8,
// tightly packed, origin top-left. Not a GPU surface.
struct Rgba8Image {
  std::uint32_t width_px = 0;
  std::uint32_t height_px = 0;
  std::vector<std::uint8_t> rgba{};
};

// Validates resource usage, rejects cycles / WAW / unbound samples, eliminates
// passes that cannot reach `output_resource`, and emits a topological order
// with write→sample barriers. Never touches a Renderer.
[[nodiscard]] std::expected<CompiledRenderGraph, diagnostics::Error>
compile_render_graph(const RenderGraphDesc &graph);

// Submits live passes in compiled order. Compile/validation failures return
// before any `Renderer::submit` call.
[[nodiscard]] std::expected<void, diagnostics::Error>
execute_render_graph(Renderer &renderer, const RenderGraphDesc &graph,
                     const CompiledRenderGraph &compiled);

// Convenience: compile then execute. Invalid graphs leave the renderer unused.
[[nodiscard]] std::expected<CompiledRenderGraph, diagnostics::Error>
compile_and_execute_render_graph(Renderer &renderer,
                                 const RenderGraphDesc &graph);

// Applies clear/fill_rect in compiled order into CPU images keyed by resource
// id and returns the output resource pixels. Sampling edges enforce order only;
// there is no texture fetch op in this slice.
[[nodiscard]] std::expected<Rgba8Image, diagnostics::Error>
rasterize_render_graph(const RenderGraphDesc &graph,
                       const CompiledRenderGraph &compiled);

} // namespace instantee::render_core
