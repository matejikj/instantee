#include <instantee/render_core/render_graph.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "render_graph", message};
}

struct PassAccess {
  std::unordered_set<std::uint64_t> reads;
  std::unordered_set<std::uint64_t> writes;
};

[[nodiscard]] std::expected<PassAccess, diagnostics::Error>
collect_access(const GraphPassNode &node) {
  PassAccess access;
  for (const GraphResourceEdge &edge : node.resources) {
    if (edge.resource == 0) {
      return std::unexpected(make_invalid(
          "invalid_resource_id", "graph resource id must be non-zero"));
    }
    switch (edge.usage) {
    case ResourceUsage::sample:
      if (access.writes.contains(edge.resource)) {
        return std::unexpected(make_invalid(
            "resource_usage_conflict",
            "a pass cannot sample and color-write the same resource"));
      }
      access.reads.insert(edge.resource);
      break;
    case ResourceUsage::color_write:
      if (access.reads.contains(edge.resource)) {
        return std::unexpected(make_invalid(
            "resource_usage_conflict",
            "a pass cannot sample and color-write the same resource"));
      }
      access.writes.insert(edge.resource);
      break;
    default:
      return std::unexpected(
          make_invalid("unsupported_resource_usage", "unknown ResourceUsage"));
    }
  }

  if (node.pass.color_target == 0) {
    return std::unexpected(make_invalid(
        "unbound_color_target", "graph pass requires a non-zero color_target"));
  }
  access.writes.insert(node.pass.color_target);
  if (access.reads.contains(node.pass.color_target)) {
    return std::unexpected(
        make_invalid("resource_usage_conflict",
                     "color_target cannot also be sampled by the same pass"));
  }
  return access;
}

[[nodiscard]] std::unordered_set<std::uint64_t>
imported_set(const RenderGraphDesc &graph) {
  std::unordered_set<std::uint64_t> imported;
  for (std::uint64_t id : graph.imported_resources) {
    if (id != 0) {
      imported.insert(id);
    }
  }
  return imported;
}

[[nodiscard]] std::expected<void, diagnostics::Error>
validate_samples(const std::vector<PassAccess> &access,
                 const std::unordered_set<std::uint64_t> &imported) {
  std::unordered_set<std::uint64_t> written;
  for (const PassAccess &pass : access) {
    written.insert(pass.writes.begin(), pass.writes.end());
  }
  for (const PassAccess &pass : access) {
    for (std::uint64_t read : pass.reads) {
      if (!written.contains(read) && !imported.contains(read)) {
        return std::unexpected(
            make_invalid("unproduced_sample",
                         "sampled resource is neither written nor imported"));
      }
    }
  }
  return {};
}

[[nodiscard]] std::expected<std::vector<std::size_t>, diagnostics::Error>
topological_order(const std::vector<PassAccess> &access) {
  const std::size_t n = access.size();
  std::vector<std::vector<std::size_t>> successors(n);
  std::vector<std::size_t> indegree(n, 0);

  // Writer → reader edges. Multiple writers of one resource are rejected: this
  // slice has no explicit pass-ordering edges beyond sample dependencies.
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> writers;
  for (std::size_t i = 0; i < n; ++i) {
    for (std::uint64_t write : access[i].writes) {
      auto &list = writers[write];
      if (!list.empty()) {
        return std::unexpected(make_invalid(
            "ambiguous_resource_write",
            "multiple passes write the same resource without ordering"));
      }
      list.push_back(i);
    }
  }

  for (std::size_t reader = 0; reader < n; ++reader) {
    for (std::uint64_t read : access[reader].reads) {
      const auto it = writers.find(read);
      if (it == writers.end()) {
        continue; // imported-only sample
      }
      for (std::size_t writer : it->second) {
        if (writer == reader) {
          return std::unexpected(make_invalid(
              "resource_usage_conflict",
              "a pass cannot depend on its own write through a sample edge"));
        }
        successors[writer].push_back(reader);
        ++indegree[reader];
      }
    }
  }

  std::queue<std::size_t> ready;
  for (std::size_t i = 0; i < n; ++i) {
    if (indegree[i] == 0) {
      ready.push(i);
    }
  }

  std::vector<std::size_t> order;
  order.reserve(n);
  while (!ready.empty()) {
    const std::size_t node = ready.front();
    ready.pop();
    order.push_back(node);
    for (std::size_t next : successors[node]) {
      if (indegree[next] == 0) {
        continue;
      }
      --indegree[next];
      if (indegree[next] == 0) {
        ready.push(next);
      }
    }
  }

  if (order.size() != n) {
    return std::unexpected(
        make_invalid("render_graph_cycle",
                     "render graph contains a cycle in resource dependencies"));
  }
  return order;
}

[[nodiscard]] std::unordered_set<std::size_t>
live_passes_reaching_output(const std::vector<PassAccess> &access,
                            const std::vector<std::size_t> &order,
                            std::uint64_t output_resource) {
  // Build reverse edges: reader → writers it depends on.
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> writers;
  for (std::size_t i = 0; i < access.size(); ++i) {
    for (std::uint64_t write : access[i].writes) {
      writers[write].push_back(i);
    }
  }

  std::unordered_set<std::size_t> live;
  std::queue<std::size_t> pending;
  for (std::size_t i = 0; i < access.size(); ++i) {
    if (access[i].writes.contains(output_resource)) {
      live.insert(i);
      pending.push(i);
    }
  }

  while (!pending.empty()) {
    const std::size_t node = pending.front();
    pending.pop();
    for (std::uint64_t read : access[node].reads) {
      const auto it = writers.find(read);
      if (it == writers.end()) {
        continue;
      }
      for (std::size_t writer : it->second) {
        if (live.insert(writer).second) {
          pending.push(writer);
        }
      }
    }
  }

  (void)order;
  return live;
}

[[nodiscard]] std::vector<ResourceBarrier>
emit_barriers(const std::vector<PassAccess> &access,
              const std::vector<std::size_t> &live_order) {
  std::vector<ResourceBarrier> barriers;
  std::unordered_map<std::uint64_t, std::size_t> last_write_order_index;

  for (std::size_t ord = 0; ord < live_order.size(); ++ord) {
    const std::size_t pass_index = live_order[ord];
    for (std::uint64_t read : access[pass_index].reads) {
      const auto it = last_write_order_index.find(read);
      if (it == last_write_order_index.end()) {
        continue; // imported
      }
      barriers.push_back(ResourceBarrier{
          .resource = read,
          .kind = BarrierKind::color_write_to_sample,
          .after_order_index = it->second,
          .before_order_index = ord,
      });
    }
    for (std::uint64_t write : access[pass_index].writes) {
      last_write_order_index[write] = ord;
    }
  }
  return barriers;
}

void fill_rect(Rgba8Image &image, const Rect2f &rect, Rgba8 color) {
  if (image.width_px == 0 || image.height_px == 0 || image.rgba.empty()) {
    return;
  }
  const float x0f = std::floor(rect.x);
  const float y0f = std::floor(rect.y);
  const float x1f = std::ceil(rect.x + rect.width);
  const float y1f = std::ceil(rect.y + rect.height);
  const std::int64_t x0 =
      std::max<std::int64_t>(0, static_cast<std::int64_t>(x0f));
  const std::int64_t y0 =
      std::max<std::int64_t>(0, static_cast<std::int64_t>(y0f));
  const std::int64_t x1 =
      std::min<std::int64_t>(static_cast<std::int64_t>(image.width_px),
                             static_cast<std::int64_t>(x1f));
  const std::int64_t y1 =
      std::min<std::int64_t>(static_cast<std::int64_t>(image.height_px),
                             static_cast<std::int64_t>(y1f));
  for (std::int64_t y = y0; y < y1; ++y) {
    for (std::int64_t x = x0; x < x1; ++x) {
      const std::size_t idx = (static_cast<std::size_t>(y) * image.width_px +
                               static_cast<std::size_t>(x)) *
                              4U;
      image.rgba[idx] = color.r;
      image.rgba[idx + 1] = color.g;
      image.rgba[idx + 2] = color.b;
      image.rgba[idx + 3] = color.a;
    }
  }
}

[[nodiscard]] std::expected<void, diagnostics::Error>
apply_pass_cpu(Rgba8Image &image, const PassDesc &pass) {
  auto valid = validate_pass(pass);
  if (!valid) {
    return valid;
  }
  if (image.width_px != pass.width_px || image.height_px != pass.height_px) {
    image.width_px = pass.width_px;
    image.height_px = pass.height_px;
    const std::size_t bytes = static_cast<std::size_t>(pass.width_px) *
                              static_cast<std::size_t>(pass.height_px) * 4U;
    if (bytes / 4U / pass.width_px != pass.height_px) {
      return std::unexpected(
          make_invalid("image_size_overflow", "pass extent overflows buffer"));
    }
    image.rgba.assign(bytes, 0);
  }
  for (const DrawOp &op : pass.ops) {
    switch (op.kind) {
    case DrawOpKind::clear:
      for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        image.rgba[i] = op.color.r;
        image.rgba[i + 1] = op.color.g;
        image.rgba[i + 2] = op.color.b;
        image.rgba[i + 3] = op.color.a;
      }
      break;
    case DrawOpKind::fill_rect:
      fill_rect(image, op.rect, op.color);
      break;
    default:
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown DrawOpKind"));
    }
  }
  return {};
}

} // namespace

std::expected<CompiledRenderGraph, diagnostics::Error>
compile_render_graph(const RenderGraphDesc &graph) {
  if (graph.passes.empty()) {
    return std::unexpected(
        make_invalid("empty_render_graph", "render graph has no passes"));
  }
  if (graph.output_resource == 0) {
    return std::unexpected(make_invalid("invalid_output_resource",
                                        "output_resource must be non-zero"));
  }

  std::vector<PassAccess> access;
  access.reserve(graph.passes.size());
  for (const GraphPassNode &node : graph.passes) {
    auto valid_pass = validate_pass(node.pass);
    if (!valid_pass) {
      return std::unexpected(valid_pass.error());
    }
    auto collected = collect_access(node);
    if (!collected) {
      return std::unexpected(collected.error());
    }
    access.push_back(std::move(*collected));
  }

  const auto imported = imported_set(graph);
  if (auto samples = validate_samples(access, imported); !samples) {
    return std::unexpected(samples.error());
  }

  auto order = topological_order(access);
  if (!order) {
    return std::unexpected(order.error());
  }

  const auto live =
      live_passes_reaching_output(access, *order, graph.output_resource);
  if (live.empty()) {
    return std::unexpected(make_invalid(
        "output_unproduced", "no pass writes the graph output_resource"));
  }

  CompiledRenderGraph compiled;
  compiled.output_resource = graph.output_resource;
  compiled.pass_order.reserve(live.size());
  for (std::size_t index : *order) {
    if (live.contains(index)) {
      compiled.pass_order.push_back(index);
    }
  }
  if (compiled.pass_order.empty()) {
    return std::unexpected(make_invalid(
        "output_unproduced", "live pass set is empty after elimination"));
  }
  compiled.barriers = emit_barriers(access, compiled.pass_order);
  return compiled;
}

std::expected<void, diagnostics::Error>
execute_render_graph(Renderer &renderer, const RenderGraphDesc &graph,
                     const CompiledRenderGraph &compiled) {
  if (compiled.pass_order.empty()) {
    return std::unexpected(
        make_invalid("empty_compiled_graph", "compiled graph has no passes"));
  }
  if (compiled.output_resource == 0 ||
      compiled.output_resource != graph.output_resource) {
    return std::unexpected(make_invalid(
        "compiled_graph_mismatch",
        "compiled output_resource does not match the source graph"));
  }
  for (std::size_t index : compiled.pass_order) {
    if (index >= graph.passes.size()) {
      return std::unexpected(make_invalid(
          "compiled_graph_mismatch", "compiled pass index is out of range"));
    }
    auto submitted = renderer.submit(graph.passes[index].pass);
    if (!submitted) {
      return submitted;
    }
  }
  return {};
}

std::expected<CompiledRenderGraph, diagnostics::Error>
compile_and_execute_render_graph(Renderer &renderer,
                                 const RenderGraphDesc &graph) {
  auto compiled = compile_render_graph(graph);
  if (!compiled) {
    return std::unexpected(compiled.error());
  }
  auto executed = execute_render_graph(renderer, graph, *compiled);
  if (!executed) {
    return std::unexpected(executed.error());
  }
  return compiled;
}

std::expected<Rgba8Image, diagnostics::Error>
rasterize_render_graph(const RenderGraphDesc &graph,
                       const CompiledRenderGraph &compiled) {
  if (compiled.pass_order.empty()) {
    return std::unexpected(
        make_invalid("empty_compiled_graph", "compiled graph has no passes"));
  }
  std::unordered_map<std::uint64_t, Rgba8Image> images;
  for (std::size_t index : compiled.pass_order) {
    if (index >= graph.passes.size()) {
      return std::unexpected(make_invalid(
          "compiled_graph_mismatch", "compiled pass index is out of range"));
    }
    const PassDesc &pass = graph.passes[index].pass;
    Rgba8Image &image = images[pass.color_target];
    auto applied = apply_pass_cpu(image, pass);
    if (!applied) {
      return std::unexpected(applied.error());
    }
  }
  const auto it = images.find(compiled.output_resource);
  if (it == images.end()) {
    return std::unexpected(make_invalid(
        "output_unproduced", "rasterizer has no image for output_resource"));
  }
  return it->second;
}

} // namespace instantee::render_core
