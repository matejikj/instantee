#include <instantee/render_core/paper_curvature.hpp>

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

void BM_PaperCurvatureBend(benchmark::State &state) {
  const auto grid = static_cast<std::uint32_t>(state.range(0));
  const auto tier = paper_curvature_tier_lite_v1();
  auto mesh = make_paper_curvature_grid(120.0, 80.0, grid, grid, tier);
  if (!mesh) {
    state.SkipWithError(mesh.error().message.c_str());
    return;
  }
  PaperCurvatureParams params;
  params.mode = PaperCurvatureMode::bend;
  params.bend_angle_rad = 0.8;
  std::uint64_t ops = 0;
  for (auto _ : state) {
    auto pose = evaluate_paper_curvature(*mesh, params, tier);
    if (!pose) {
      state.SkipWithError(pose.error().message.c_str());
      return;
    }
    ops = pose->ops_used;
    benchmark::DoNotOptimize(pose->vertices.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(
                                                   mesh->bind_vertices.size()));
  state.counters["ops_used"] = benchmark::Counter(static_cast<double>(ops));
  state.counters["ops_budget"] =
      benchmark::Counter(static_cast<double>(tier.max_total_ops));
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

void BM_PaperCurvatureWave(benchmark::State &state) {
  const auto grid = static_cast<std::uint32_t>(state.range(0));
  const auto tier = paper_curvature_tier_lite_v1();
  auto mesh = make_paper_curvature_grid(120.0, 80.0, grid, grid, tier);
  if (!mesh) {
    state.SkipWithError(mesh.error().message.c_str());
    return;
  }
  PaperCurvatureParams params;
  params.mode = PaperCurvatureMode::wave;
  params.wave_amp_px = 6.0;
  params.wave_freq_u = 2.0;
  std::uint64_t ops = 0;
  for (auto _ : state) {
    auto pose = evaluate_paper_curvature(*mesh, params, tier);
    if (!pose) {
      state.SkipWithError(pose.error().message.c_str());
      return;
    }
    ops = pose->ops_used;
    benchmark::DoNotOptimize(pose->vertices.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(
                                                   mesh->bind_vertices.size()));
  state.counters["ops_used"] = benchmark::Counter(static_cast<double>(ops));
  state.counters["ops_budget"] =
      benchmark::Counter(static_cast<double>(tier.max_total_ops));
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

} // namespace

BENCHMARK(BM_PaperCurvatureBend)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_PaperCurvatureWave)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
