#include <instantee/typography/glyph_atlas.hpp>

#include <benchmark/benchmark.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

using namespace instantee::typography;

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

[[nodiscard]] std::filesystem::path font_dir() {
  return std::filesystem::path{INSTANTEE_PINNED_FONT_DIR};
}

struct Fixture {
  FontStack fonts;
  TextShaper shaper{};
};

[[nodiscard]] Fixture make_fixture() {
  const auto primary = font_dir() / "NotoSans-Regular.ttf";
  const std::filesystem::path paths[] = {primary};
  auto fonts = FontStack::open(paths);
  if (!fonts) {
    throw std::runtime_error(fonts.error().message);
  }
  return Fixture{.fonts = std::move(*fonts)};
}

void BM_GlyphAtlasStaticText(benchmark::State &state) {
  Fixture fixture = make_fixture();
  GlyphAtlasConfig cfg;
  cfg.atlas.page_width_px = 512;
  cfg.atlas.page_height_px = 512;
  cfg.atlas.padding_px = 1;
  cfg.max_cached_glyphs = 1024;
  cfg.max_cached_layouts = 64;

  constexpr std::string_view kText =
      "Příliš žluťoučký kůň úpěl ďábelské ódy — static";
  constexpr std::string_view kFp = "noto-sans";
  constexpr float kSize = 32.0f;

  std::uint64_t warm_shapes = 0;
  std::uint64_t warm_rasters = 0;
  std::uint64_t layout_hits = 0;
  std::uint64_t layout_misses = 0;

  for (auto _ : state) {
    GlyphAtlas atlas(cfg);
    if (!atlas.begin_frame(1)) {
      state.SkipWithError("begin_frame failed");
      return;
    }
    auto cold = atlas.prepare(fixture.shaper, fixture.fonts, kFp, kText, kSize);
    if (!cold) {
      state.SkipWithError(cold.error().message.c_str());
      return;
    }
    if (!atlas.end_frame(1)) {
      state.SkipWithError("end_frame failed");
      return;
    }

    const auto shapes_after_cold = atlas.stats().shape_calls;
    const auto rasters_after_cold = atlas.stats().raster_calls;

    if (!atlas.begin_frame(2)) {
      state.SkipWithError("begin_frame warm failed");
      return;
    }
    auto warm = atlas.prepare(fixture.shaper, fixture.fonts, kFp, kText, kSize);
    if (!warm || !warm->from_layout_cache) {
      state.SkipWithError("warm prepare missed layout cache");
      return;
    }
    if (!atlas.end_frame(2)) {
      state.SkipWithError("end_frame warm failed");
      return;
    }

    warm_shapes = atlas.stats().shape_calls - shapes_after_cold;
    warm_rasters = atlas.stats().raster_calls - rasters_after_cold;
    layout_hits = atlas.stats().layout_hits;
    layout_misses = atlas.stats().layout_misses;
    benchmark::DoNotOptimize(warm->glyphs.data());
  }

  state.SetItemsProcessed(state.iterations());
  state.counters["warm_shape_calls"] =
      benchmark::Counter(static_cast<double>(warm_shapes));
  state.counters["warm_raster_calls"] =
      benchmark::Counter(static_cast<double>(warm_rasters));
  state.counters["layout_hits"] =
      benchmark::Counter(static_cast<double>(layout_hits));
  state.counters["layout_misses"] =
      benchmark::Counter(static_cast<double>(layout_misses));
  state.counters["rss_peak_bytes"] =
      benchmark::Counter(static_cast<double>(rss_peak_bytes()));
}

BENCHMARK(BM_GlyphAtlasStaticText)->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
