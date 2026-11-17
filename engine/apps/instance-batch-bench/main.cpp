#include <instantee/render_core/instance_batch.hpp>
#include <instantee/render_core/primitives.hpp>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>

#if defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

using namespace instantee::render_core;

[[nodiscard]] std::uint64_t rss_peak_bytes() {
#if defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
  return usage.ru_maxrss > 0 ? static_cast<std::uint64_t>(usage.ru_maxrss) : 0;
#else
  return 0;
#endif
}

[[nodiscard]] PassDesc make_pass(std::int64_t instance_count,
                                 std::int64_t texture_groups) {
  PassDesc pass;
  pass.width_px = 256;
  pass.height_px = 256;
  pass.color_target = 1;
  pass.ops.push_back(
      DrawOp{.kind = DrawOpKind::clear, .color = {0, 0, 0, 255}});
  const auto groups = texture_groups <= 0 ? std::int64_t{1} : texture_groups;
  // Contiguous runs per texture_binding so the batcher can coalesce.
  for (std::int64_t group = 0; group < groups; ++group) {
    const auto binding = static_cast<std::uint64_t>(group) + 1U;
    const std::int64_t begin = (instance_count * group) / groups;
    const std::int64_t end = (instance_count * (group + 1)) / groups;
    for (std::int64_t i = begin; i < end; ++i) {
      pass.ops.push_back(DrawOp{
          .kind = DrawOpKind::fill_rect,
          .rect = {static_cast<float>(i % 200),
                   static_cast<float>((i * 3) % 200), 8, 8},
          .color = {255, static_cast<std::uint8_t>(i % 255), 0, 255},
          .texture_binding = binding,
      });
    }
  }
  return pass;
}

void BM_BatchPassPlan(benchmark::State &state) {
  const auto instances = state.range(0);
  const auto groups = state.range(1);
  const PassDesc pass = make_pass(instances, groups);
  std::uint32_t draw_calls = 0;
  std::uint32_t source_ops = 0;
  for (auto _ : state) {
    auto batched = batch_pass(pass);
    if (!batched) {
      state.SkipWithError(batched.error().message.c_str());
      return;
    }
    draw_calls = batched->stats.draw_calls;
    source_ops = batched->stats.source_ops;
    benchmark::DoNotOptimize(batched->batches.data());
  }
  state.SetItemsProcessed(state.iterations() * instances);
  state.counters["source_ops"] = benchmark::Counter(source_ops);
  state.counters["draw_calls"] = benchmark::Counter(draw_calls);
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

void BM_RasterizeBatched(benchmark::State &state) {
  const auto instances = state.range(0);
  const PassDesc pass = make_pass(instances, 1);
  auto batched = batch_pass(pass);
  if (!batched) {
    std::cerr << "fixture batch failed\n";
    std::exit(2);
  }
  Rgba8Image image;
  for (auto _ : state) {
    auto ok = rasterize_batched_pass(image, *batched);
    if (!ok) {
      state.SkipWithError(ok.error().message.c_str());
      return;
    }
    benchmark::DoNotOptimize(image.rgba.data());
  }
  state.SetItemsProcessed(state.iterations() * instances);
  state.counters["source_ops"] = benchmark::Counter(batched->stats.source_ops);
  state.counters["draw_calls"] = benchmark::Counter(batched->stats.draw_calls);
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

} // namespace

BENCHMARK(BM_BatchPassPlan)
    ->Args({64, 1})
    ->Args({256, 1})
    ->Args({1024, 1})
    ->Args({1024, 8})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_RasterizeBatched)
    ->Args({64})
    ->Args({256})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
