#include <instantee/asset_pipeline/texture_atlas.hpp>

#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

using namespace instantee::asset_pipeline;

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

[[nodiscard]] std::vector<std::uint8_t>
make_sprite(std::uint32_t w, std::uint32_t h, std::uint8_t seed) {
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4U);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = seed;
    rgba[i + 1] = static_cast<std::uint8_t>(seed * 3U);
    rgba[i + 2] = static_cast<std::uint8_t>(seed * 7U);
    rgba[i + 3] = 255;
  }
  return rgba;
}

void BM_AtlasPackAndUpload(benchmark::State &state) {
  const auto sprite_count = static_cast<std::int64_t>(state.range(0));
  const std::uint32_t side = static_cast<std::uint32_t>(state.range(1));

  TextureAtlasConfig cfg;
  cfg.page_width_px = 512;
  cfg.page_height_px = 512;
  cfg.padding_px = 1;
  cfg.max_pages = 16;
  cfg.max_sprites = static_cast<std::uint32_t>(sprite_count) + 8U;
  cfg.mip_policy = AtlasMipPolicy::none;
  cfg.sample_filter = AtlasSampleFilter::linear;

  std::vector<std::vector<std::uint8_t>> sprites;
  sprites.reserve(static_cast<std::size_t>(sprite_count));
  for (std::int64_t i = 0; i < sprite_count; ++i) {
    sprites.push_back(
        make_sprite(side, side, static_cast<std::uint8_t>(i & 0xff)));
  }

  std::uint64_t hits = 0;
  std::uint64_t misses = 0;
  std::uint64_t staging = 0;
  std::uint64_t warm_staging = 0;

  for (auto _ : state) {
    TextureAtlas atlas(cfg);
    for (std::int64_t i = 0; i < sprite_count; ++i) {
      const std::string key = "s" + std::to_string(i);
      auto inserted =
          atlas.insert(key, side, side, sprites[static_cast<std::size_t>(i)]);
      if (!inserted) {
        state.SkipWithError(inserted.error().message.c_str());
        return;
      }
    }
    auto batch = atlas.plan_uploads();
    if (!batch) {
      state.SkipWithError(batch.error().message.c_str());
      return;
    }
    atlas.mark_uploads_resident(*batch);
    // Warm path: re-insert same keys and plan again — must not restage.
    for (std::int64_t i = 0; i < sprite_count; ++i) {
      const std::string key = "s" + std::to_string(i);
      auto again =
          atlas.insert(key, side, side, sprites[static_cast<std::size_t>(i)]);
      if (!again) {
        state.SkipWithError(again.error().message.c_str());
        return;
      }
    }
    auto warm = atlas.plan_uploads();
    if (!warm) {
      state.SkipWithError(warm.error().message.c_str());
      return;
    }
    hits = atlas.stats().insert_hits;
    misses = atlas.stats().insert_misses;
    staging = batch->staging_bytes;
    warm_staging = warm->staging_bytes;
    benchmark::DoNotOptimize(batch->staging_rgba.data());
  }

  state.SetItemsProcessed(state.iterations() * sprite_count);
  state.counters["insert_hits"] = benchmark::Counter(static_cast<double>(hits));
  state.counters["insert_misses"] =
      benchmark::Counter(static_cast<double>(misses));
  state.counters["hit_rate"] =
      benchmark::Counter(misses == 0 ? 0.0
                                     : static_cast<double>(hits) /
                                           static_cast<double>(hits + misses));
  state.counters["staging_bytes"] =
      benchmark::Counter(static_cast<double>(staging));
  state.counters["warm_staging_bytes"] =
      benchmark::Counter(static_cast<double>(warm_staging));
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

} // namespace

BENCHMARK(BM_AtlasPackAndUpload)
    ->Args({16, 8})
    ->Args({64, 8})
    ->Args({256, 16})
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
