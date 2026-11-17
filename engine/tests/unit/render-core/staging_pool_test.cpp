#include <instantee/render_core/staging_pool.hpp>

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] bool matches(std::span<const std::uint8_t> bytes,
                           std::uint8_t stamp) {
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] != static_cast<std::uint8_t>((stamp + i) & 0xffU)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool all_of_zero(std::span<const std::uint8_t> bytes) {
  for (std::uint8_t value : bytes) {
    if (value != 0) {
      return false;
    }
  }
  return true;
}

void paint(std::span<std::uint8_t> bytes, std::uint8_t stamp) {
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::uint8_t>((stamp + i) & 0xffU);
  }
}

} // namespace

int main() {
  using namespace instantee::render_core;

  StagingPoolConfig config{.capacity = 2, .width_px = 4, .height_px = 4};
  auto pool = StagingBufferPool::create(config);
  expect(pool.has_value(), "create staging pool");
  if (!pool) {
    return EXIT_FAILURE;
  }

  // Threshold: no fallback allocation past capacity.
  {
    auto a = pool->try_acquire();
    auto b = pool->try_acquire();
    expect(a && b, "fill capacity");
    auto c = pool->try_acquire();
    expect(!c.has_value(), "threshold rejects");
    if (!c) {
      expect(c.error().code == "would_block", "would_block code");
      expect(c.error().message.find("no fallback") != std::string::npos,
             "no fallback message");
    }
    expect(pool->stats().acquire_rejections >= 1, "rejection counted");
    a->release();
    b->release();
  }

  // Delayed GPU callback + cancel must not overwrite busy bytes.
  {
    auto lease = pool->try_acquire();
    expect(lease.has_value(), "busy acquire");
    auto pixels = lease->pixels();
    expect(pixels.has_value(), "writable pixels");
    paint(*pixels, 40);
    expect(lease->mark_gpu_busy().has_value(), "gpu busy");
    expect(!lease->pixels().has_value(), "mutation frozen");
    lease->cancel();
    expect(lease->state() == StagingLease::State::cancelled, "cancelled");
    auto frozen = lease->frozen_bytes();
    expect(frozen.has_value() && matches(*frozen, 40),
           "busy bytes intact after cancel");
    auto other = pool->try_acquire();
    expect(other.has_value(), "other free slot still available");
    expect(lease->complete_gpu().has_value(), "gpu complete after cancel");
    expect(lease->empty(), "recycled after hold drain");
    auto again = pool->try_acquire();
    expect(again.has_value(), "acquire recycled");
    auto cleared = again->frozen_bytes();
    expect(cleared.has_value() && all_of_zero(*cleared),
           "zeroed after recycle");
  }

  // Interop: staging → sink copy; sink hold blocks recycle; delayed sink
  // completion then frees the slot without ever growing the pool.
  {
    StagingPoolConfig one{.capacity = 1, .width_px = 2, .height_px = 2};
    auto interop = StagingBufferPool::create(one);
    expect(interop.has_value(), "interop pool");
    auto staging = interop->try_acquire();
    expect(staging.has_value(), "interop staging");
    auto src = staging->pixels();
    expect(src.has_value(), "interop writable");
    paint(*src, 7);

    std::vector<std::uint8_t> sink(src->size(), 0);
    expect(staging->mark_gpu_busy().has_value(), "interop gpu");
    auto frozen = staging->frozen_bytes();
    expect(frozen.has_value(), "interop frozen");
    expect(copy_staging_to_sink(*frozen, sink).has_value(), "copy to sink");
    expect(matches(sink, 7), "sink received staging bytes");
    expect(staging->mark_sink_busy().has_value(), "sink hold");
    expect(staging->complete_gpu().has_value(), "gpu done");
    expect(!staging->empty(), "still held by sink");
    auto rejected = interop->try_acquire();
    expect(!rejected.has_value(), "capacity blocked by sink");
    if (!rejected) {
      expect(rejected.error().code == "would_block", "interop threshold");
    }
    staging->cancel();
    expect(matches(*staging->frozen_bytes(), 7), "sink-busy data intact");
    expect(staging->complete_sink().has_value(), "sink complete");
    expect(staging->empty(), "recycled after sink");
    auto reused = interop->try_acquire();
    expect(reused.has_value(), "slot reused after sink");
    expect(interop->stats().capacity == 1, "capacity unchanged");
    expect(all_of_zero(*reused->frozen_bytes()), "no previous frame leak");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
