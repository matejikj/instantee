#include <instantee/runtime/frame_eval_scheduler.hpp>
#include <instantee/runtime/frame_evaluator.hpp>

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {

using namespace instantee::core;
using namespace instantee::runtime;

[[nodiscard]] std::uint64_t rss_peak_bytes() {
#if defined(__APPLE__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
  // Darwin ru_maxrss is bytes.
  return usage.ru_maxrss > 0 ? static_cast<std::uint64_t>(usage.ru_maxrss) : 0;
#else
  return 0;
#endif
}

struct FixtureScene {
  std::unique_ptr<const CompiledScene> scene;
  AnimationProgram program;
};

[[nodiscard]] FixtureScene make_fixture(std::uint64_t duration_frames,
                                        std::uint32_t node_count) {
  CompiledSceneDraft draft;
  draft.canvas_width = 1080;
  draft.canvas_height = 1920;
  draft.duration_frames = FrameIndex{duration_frames};
  draft.seed = 42;
  draft.node_ids.reserve(node_count);
  draft.nodes.reserve(node_count);

  for (std::uint32_t index = 0; index < node_count; ++index) {
    draft.node_ids.push_back("n" + std::to_string(index));
    RuntimeNode node;
    node.kind = index == 0 ? RuntimeNodeKind::group : RuntimeNodeKind::geometry;
    if (index > 0) {
      node.parent_index = 0;
      node.blend = BlendMode::premultiplied_alpha;
    }
    node.transform.opacity = 1.0;
    node.transform.translate_x = static_cast<double>(index);
    draft.nodes.push_back(node);
  }

  auto scene = make_compiled_scene(std::move(draft));
  if (!scene) {
    std::cerr << "fixture scene failed\n";
    std::exit(2);
  }

  FixtureScene out;
  out.scene = std::move(*scene);
  if (node_count > 1) {
    auto track = make_scalar_track(
        {
            ScalarKeyframe{FrameIndex{0}, 0.0},
            ScalarKeyframe{FrameIndex{duration_frames / 2}, 1.0},
            ScalarKeyframe{FrameIndex{duration_frames - 1}, 0.25},
        },
        TrackInterpolation::linear);
    if (!track) {
      std::cerr << "fixture track failed\n";
      std::exit(2);
    }
    out.program.scalar_bindings.push_back(ScalarPropertyBinding{
        .node_index = 1,
        .channel = AnimatableChannel::opacity,
        .track = *track,
    });
  }
  return out;
}

void BM_EvaluateFrameSequential(benchmark::State &state) {
  const auto duration = static_cast<std::uint64_t>(state.range(0));
  const auto nodes = static_cast<std::uint32_t>(state.range(1));
  auto fixture = make_fixture(duration, nodes);
  std::uint64_t frame = 0;
  for (auto _ : state) {
    auto evaluated =
        evaluate_frame(*fixture.scene, fixture.program, FrameIndex{frame});
    benchmark::DoNotOptimize(evaluated);
    if (!evaluated) {
      state.SkipWithError("evaluate_frame failed");
      return;
    }
    frame = (frame + 1) % duration;
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["nodes"] = benchmark::Counter(nodes);
  state.counters["rss_peak_bytes"] = benchmark::Counter(
      static_cast<double>(rss_peak_bytes()), benchmark::Counter::kDefaults,
      benchmark::Counter::OneK::kIs1024);
}

void BM_EvaluateFrameRandomAccess(benchmark::State &state) {
  const auto duration = static_cast<std::uint64_t>(state.range(0));
  const auto nodes = static_cast<std::uint32_t>(state.range(1));
  auto fixture = make_fixture(duration, nodes);
  // Deterministic permutation without depending on evaluation order state.
  std::uint64_t cursor = 0;
  for (auto _ : state) {
    const std::uint64_t frame = (cursor * 2654435761ull) % duration;
    auto evaluated =
        evaluate_frame(*fixture.scene, fixture.program, FrameIndex{frame});
    benchmark::DoNotOptimize(evaluated);
    if (!evaluated) {
      state.SkipWithError("evaluate_frame failed");
      return;
    }
    ++cursor;
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["nodes"] = benchmark::Counter(nodes);
}

void BM_SchedulerPipeline(benchmark::State &state) {
  const auto frames = static_cast<std::uint64_t>(state.range(0));
  const auto workers = static_cast<std::uint32_t>(state.range(1));

  for (auto _ : state) {
    state.PauseTiming();
    FrameEvalScheduler::Config config;
    config.credit_capacity = 2;
    config.queue_capacity = 1;
    config.worker_count = workers;
    auto scheduler = FrameEvalScheduler::create(
        config,
        [](std::uint64_t frame, std::stop_token)
            -> std::expected<void, instantee::diagnostics::Error> {
          // Tiny CPU body; backpressure comes from the sink cadence below.
          benchmark::DoNotOptimize(frame);
          return {};
        });
    if (!scheduler) {
      state.SkipWithError("scheduler create failed");
      return;
    }

    std::vector<std::uint64_t> received;
    received.reserve(static_cast<std::size_t>(frames));
    state.ResumeTiming();

    std::jthread sink([&](std::stop_token stop) {
      for (;;) {
        auto next = (*scheduler)->take_next(stop);
        if (!next) {
          break;
        }
        // Controlled slow sink to exercise queue waits without busy-spin.
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        received.push_back(next->frame_index);
      }
    });

    for (std::uint64_t frame = 0; frame < frames; ++frame) {
      auto submitted = (*scheduler)->submit(frame);
      if (!submitted) {
        state.SkipWithError("submit failed");
        (*scheduler)->abort();
        sink.request_stop();
        return;
      }
    }
    (*scheduler)->close();
    sink.join();

    state.PauseTiming();
    const auto metrics = (*scheduler)->queue_metrics();
    state.counters["blocked_push"] =
        benchmark::Counter(static_cast<double>(metrics.blocked_push_count));
    state.counters["blocked_wait_ns"] =
        benchmark::Counter(static_cast<double>(metrics.blocked_wait_ns));
    state.counters["queue_high_water"] =
        benchmark::Counter(static_cast<double>(metrics.high_water));
    if (received.size() != frames) {
      state.SkipWithError("incomplete drain");
      return;
    }
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * frames);
}

void BM_AllocationGrowthProbe(benchmark::State &state) {
  // Not a timed micrometric: reports RSS after fixed frame batches so the
  // Python packager can prove per-frame RSS growth plateaus (no unbounded
  // heap climb across batch sizes).
  const auto batch = static_cast<std::uint64_t>(state.range(0));
  auto fixture = make_fixture(4096, 32);
  for (auto _ : state) {
    state.PauseTiming();
    const auto before = rss_peak_bytes();
    state.ResumeTiming();
    for (std::uint64_t frame = 0; frame < batch; ++frame) {
      auto evaluated =
          evaluate_frame(*fixture.scene, fixture.program, FrameIndex{frame});
      benchmark::DoNotOptimize(evaluated);
      if (!evaluated) {
        state.SkipWithError("evaluate_frame failed");
        return;
      }
    }
    state.PauseTiming();
    const auto after = rss_peak_bytes();
    state.counters["rss_before_bytes"] = benchmark::Counter(
        static_cast<double>(before), benchmark::Counter::kDefaults,
        benchmark::Counter::OneK::kIs1024);
    state.counters["rss_after_bytes"] = benchmark::Counter(
        static_cast<double>(after), benchmark::Counter::kDefaults,
        benchmark::Counter::OneK::kIs1024);
    state.counters["batch_frames"] =
        benchmark::Counter(static_cast<double>(batch));
    state.ResumeTiming();
  }
}

// Register by name instead of BENCHMARK() macros: those expand __COUNTER__,
// which clang-tidy flags as a C2y extension under -Werror.
void register_benchmarks() {
  benchmark::RegisterBenchmark("BM_EvaluateFrameSequential",
                               BM_EvaluateFrameSequential)
      ->Args({256, 8})
      ->Args({1024, 32})
      ->Args({4096, 64})
      ->Unit(benchmark::kMicrosecond)
      ->MeasureProcessCPUTime()
      ->UseRealTime();

  benchmark::RegisterBenchmark("BM_EvaluateFrameRandomAccess",
                               BM_EvaluateFrameRandomAccess)
      ->Args({1024, 32})
      ->Args({4096, 64})
      ->Unit(benchmark::kMicrosecond)
      ->MeasureProcessCPUTime()
      ->UseRealTime();

  benchmark::RegisterBenchmark("BM_SchedulerPipeline", BM_SchedulerPipeline)
      ->Args({64, 1})
      ->Args({128, 2})
      ->Unit(benchmark::kMillisecond)
      ->MeasureProcessCPUTime()
      ->UseRealTime();

  benchmark::RegisterBenchmark("BM_AllocationGrowthProbe",
                               BM_AllocationGrowthProbe)
      ->Args({256})
      ->Args({512})
      ->Args({1024})
      ->Args({2048})
      ->Unit(benchmark::kMillisecond)
      ->MeasureProcessCPUTime()
      ->UseRealTime();
}

int run_benchmarks(int argc, char **argv) {
  register_benchmarks();
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

} // namespace

int main(int argc, char **argv) { return run_benchmarks(argc, argv); }
