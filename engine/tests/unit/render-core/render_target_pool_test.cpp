#include <instantee/render_core/render_target_pool.hpp>

#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <utility>

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

void fill_pattern(std::span<std::uint8_t> pixels, std::uint8_t stamp) {
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::uint8_t>((stamp + i) & 0xffU);
  }
}

[[nodiscard]] bool all_zero(std::span<const std::uint8_t> pixels) {
  for (std::uint8_t value : pixels) {
    if (value != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool all_color(std::span<const std::uint8_t> pixels,
                             instantee::render_core::Rgba8 color) {
  if (pixels.size() % 4 != 0) {
    return false;
  }
  for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
    if (pixels[i] != color.r || pixels[i + 1] != color.g ||
        pixels[i + 2] != color.b || pixels[i + 3] != color.a) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  using namespace instantee::render_core;

  RenderTargetPoolConfig config{};
  config.registry_capacity = 4;
  config.max_pooled_bytes = 1024;
  config.max_allocation_bytes = 512;
  config.clear_policy = RenderTargetClearPolicy::clear_to_transparent;

  auto pool = RenderTargetPool::create(config);
  expect(pool.has_value(), "create pool");
  if (!pool) {
    return EXIT_FAILURE;
  }

  const GpuResourceDesc small{.width_px = 4, .height_px = 4}; // 64 bytes
  const GpuResourceDesc other{.width_px = 8, .height_px = 2}; // 64 bytes

  // Reuse: second acquire of same size must not create another allocation.
  {
    auto first = pool->acquire(small);
    expect(first.has_value(), "first acquire");
    auto pixels = first->pixels();
    expect(pixels.has_value(), "first pixels");
    fill_pattern(*pixels, 11);
    const GpuResourceHandle handle = first->handle();
    first->release();

    expect(pool->stats().allocations_created == 1, "one allocation");
    expect(pool->stats().acquires_reused == 0, "no reuse yet");
    expect(pool->stats().free_list_size == 1, "one free");

    auto second = pool->acquire(small);
    expect(second.has_value(), "second acquire");
    expect(second->handle() == handle, "same handle reused");
    expect(pool->stats().allocations_created == 1, "still one allocation");
    expect(pool->stats().acquires_reused == 1, "reuse counted");
    auto cleared = second->pixels();
    expect(cleared.has_value() && all_zero(*cleared),
           "previous frame content cleared");
  }

  // Incompatible size allocates separately; no silent resize.
  {
    const auto created_before = pool->stats().allocations_created;
    auto lease = pool->acquire(other);
    expect(lease.has_value(), "other size acquire");
    expect(pool->stats().allocations_created == created_before + 1,
           "new allocation for other size");
    expect(lease->desc().width_px == 8 && lease->desc().height_px == 2,
           "desc preserved");
  }

  // Budget: reject oversized single allocation without downscale.
  {
    auto huge = pool->acquire(GpuResourceDesc{.width_px = 64, .height_px = 64});
    expect(!huge.has_value(), "over max_allocation rejected");
    if (!huge) {
      expect(huge.error().code == "resource_budget_exceeded", "budget code");
      expect(huge.error().message.find("quality is not reduced") !=
                 std::string::npos,
             "no quality downgrade message");
    }
  }

  // Budget: fill pooled bytes then reject another distinct resident size.
  {
    RenderTargetPoolConfig tight{};
    tight.registry_capacity = 4;
    tight.max_pooled_bytes = 128; // two 64-byte targets
    tight.max_allocation_bytes = 128;
    auto tight_pool = RenderTargetPool::create(tight);
    expect(tight_pool.has_value(), "tight pool");
    auto a = tight_pool->acquire(small);
    auto b = tight_pool->acquire(other);
    expect(a && b, "two targets fit");
    // Hold both so free list cannot satisfy a third compatible request; force
    // a new allocation of `small` while bytes are exhausted.
    auto c = tight_pool->acquire(small);
    expect(!c.has_value(), "pooled budget rejects third live target");
    if (!c) {
      expect(c.error().code == "resource_budget_exceeded",
             "pooled budget code");
    }
  }

  // Pressure: unused incompatible free-list residents are retired so a new
  // exact-size allocation can proceed without downscale.
  {
    RenderTargetPoolConfig cfg{};
    cfg.registry_capacity = 4;
    cfg.max_pooled_bytes = 64; // one 4x4 target at a time
    cfg.max_allocation_bytes = 64;
    auto pressure = RenderTargetPool::create(cfg);
    expect(pressure.has_value(), "pressure pool");
    {
      auto stale = pressure->acquire(other); // 8x2 = 64 bytes
      expect(stale.has_value(), "stale size");
    } // recycled onto free list
    expect(pressure->stats().free_list_size == 1, "stale on free list");
    expect(pressure->stats().bytes_currently_pooled == 64, "bytes held");
    auto next = pressure->acquire(small); // 4x4 = 64 bytes, different size
    expect(next.has_value(), "evict frees room for exact size");
    expect(pressure->stats().free_list_evictions >= 1, "eviction counted");
    expect(next->desc().width_px == 4 && next->desc().height_px == 4,
           "requested size preserved");
  }

  // Clear-to-color policy.
  {
    RenderTargetPoolConfig colored{};
    colored.registry_capacity = 2;
    colored.max_pooled_bytes = 256;
    colored.max_allocation_bytes = 256;
    colored.clear_policy = RenderTargetClearPolicy::clear_to_color;
    colored.clear_color = Rgba8{.r = 1, .g = 2, .b = 3, .a = 4};
    auto colored_pool = RenderTargetPool::create(colored);
    auto lease = colored_pool->acquire(small);
    expect(lease.has_value(), "colored acquire");
    auto pixels = lease->pixels();
    expect(pixels.has_value() && all_color(*pixels, colored.clear_color),
           "clear_to_color applied");
    fill_pattern(*pixels, 99);
    lease->release();
    auto again = colored_pool->acquire(small);
    auto cleared = again->pixels();
    expect(cleared.has_value() && all_color(*cleared, colored.clear_color),
           "recycle clears to configured color");
  }

  // Destructor returns lease to pool (no leak of in-use count).
  {
    const auto in_use_before = pool->stats().in_use_count;
    {
      auto lease = pool->acquire(small);
      expect(lease.has_value(), "scoped acquire");
      expect(pool->stats().in_use_count == in_use_before + 1, "in use bump");
    }
    expect(pool->stats().in_use_count == in_use_before, "destructor recycled");
  }

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
