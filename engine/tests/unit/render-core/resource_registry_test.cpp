#include <instantee/render_core/resource_registry.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "render-core tests must remain free of Metal/Apple headers"
#endif

namespace {

int failures = 0;
std::atomic<int> g_live_payloads{0};
std::atomic<int> g_destroy_count{0};

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct CountingPayload final : instantee::render_core::GpuResourcePayload {
  explicit CountingPayload(int id) : id(id) { ++g_live_payloads; }
  ~CountingPayload() override {
    ++g_destroy_count;
    --g_live_payloads;
  }
  int id = 0;
};

struct ThrowingPayload final : instantee::render_core::GpuResourcePayload {
  ThrowingPayload() { throw std::bad_alloc(); }
};

} // namespace

int main() {
  using namespace instantee::render_core;

  g_live_payloads = 0;
  g_destroy_count = 0;

  auto registry = GpuResourceRegistry::create(2);
  expect(registry.has_value(), "create registry");
  if (!registry) {
    return EXIT_FAILURE;
  }

  // Happy path: create → upload → resident → retain → complete → retire.
  {
    auto handle =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{8, 8}, 1);
    expect(handle.has_value(), "create resource");
    expect(registry->live_count() == 1, "one live");
    expect(registry->begin_upload(*handle).has_value(), "upload");
    expect(registry->mark_resident(*handle).has_value(), "resident");

    auto submission = registry->begin_submission();
    expect(submission.retain(*handle).has_value(), "retain");
    expect(registry->request_retire(*handle).has_value(), "retire while held");
    expect(registry->retiring_count() == 1, "retiring");
    expect(g_live_payloads.load() == 1, "payload alive during hold");

    // Stale generation after a different resource cycles should still work for
    // this live handle; resolve by generation.
    auto state = registry->state(*handle);
    expect(state.has_value() && *state == GpuResourceState::retiring,
           "state retiring");

    expect(submission.complete().has_value(), "complete");
    expect(g_live_payloads.load() == 0, "payload destroyed after complete");
    expect(registry->live_count() == 0, "no live after release");
    expect(registry->retiring_count() == 0, "no retiring after release");

    auto stale = registry->state(*handle);
    expect(!stale.has_value(), "stale handle rejected");
    if (!stale) {
      expect(stale.error().code == "stale_resource_handle", "stale code");
    }
  }

  // Partial init: null payload publishes nothing.
  {
    auto bad = registry->create_resource(GpuResourceDesc{4, 4}, nullptr);
    expect(!bad.has_value(), "null payload rejected");
    expect(registry->live_count() == 0, "no live after null payload");
  }

  // Partial init: throwing emplace leaves capacity free (no published handle).
  {
    const int destroys_before = g_destroy_count.load();
    auto threw =
        registry->emplace_resource<ThrowingPayload>(GpuResourceDesc{4, 4});
    expect(!threw.has_value(), "throwing payload rejected");
    expect(threw.error().code == "resource_budget_exceeded", "alloc code");
    expect(registry->live_count() == 0, "no live after throw");
    expect(g_destroy_count.load() == destroys_before, "no destroy on throw");
  }

  // Capacity bound.
  {
    auto a =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{1, 1}, 2);
    auto b =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{1, 1}, 3);
    expect(a && b, "fill capacity");
    auto c =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{1, 1}, 4);
    expect(!c.has_value(), "capacity exceeded");
    expect(c.error().code == "resource_budget_exceeded", "budget code");
    expect(registry->request_retire(*a).has_value(), "free a");
    expect(registry->request_retire(*b).has_value(), "free b");
    expect(g_live_payloads.load() == 0, "payloads freed");
  }

  // Delayed completion: retire under hold, complete later — single destroy.
  {
    const int destroys_before = g_destroy_count.load();
    auto handle =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{2, 2}, 5);
    expect(handle.has_value(), "delayed create");
    expect(registry->begin_upload(*handle).has_value(), "delayed upload");
    expect(registry->mark_resident(*handle).has_value(), "delayed resident");

    auto submission = registry->begin_submission();
    expect(submission.retain(*handle).has_value(), "delayed retain");
    expect(registry->request_retire(*handle).has_value(), "delayed retire");
    expect(g_destroy_count.load() == destroys_before, "no destroy yet");
    expect(g_live_payloads.load() == 1, "still alive");

    // Use-after-retire via stale forge must fail.
    const GpuResourceHandle forged =
        GpuResourceHandle::from_raw(handle->raw() + (1ULL << 32));
    expect(!registry->try_get(forged).has_value(),
           "forged generation rejected");

    expect(submission.complete().has_value(), "delayed complete");
    expect(g_destroy_count.load() == destroys_before + 1, "single destroy");
    expect(g_live_payloads.load() == 0, "no leak");
  }

  // Cancel path: destructor of unfinished submission releases holds safely.
  {
    const int destroys_before = g_destroy_count.load();
    auto handle =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{2, 2}, 6);
    expect(registry->begin_upload(*handle).has_value(), "cancel upload");
    expect(registry->mark_resident(*handle).has_value(), "cancel resident");
    {
      auto submission = registry->begin_submission();
      expect(submission.retain(*handle).has_value(), "cancel retain");
      expect(registry->request_retire(*handle).has_value(), "cancel retire");
      expect(g_live_payloads.load() == 1, "alive until cancel scope ends");
      // No complete/cancel: destructor runs release_holds.
    }
    expect(g_destroy_count.load() == destroys_before + 1,
           "cancel destroys once");
    expect(g_live_payloads.load() == 0, "cancel no leak");
    expect(!registry->try_get(*handle).has_value(),
           "handle stale after cancel");
  }

  // Explicit cancel + mark_failed under hold.
  {
    const int destroys_before = g_destroy_count.load();
    auto handle =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{2, 2}, 7);
    expect(registry->begin_upload(*handle).has_value(), "fail upload");
    expect(registry->mark_resident(*handle).has_value(), "fail resident");
    auto submission = registry->begin_submission();
    expect(submission.retain(*handle).has_value(), "fail retain");
    expect(registry->mark_failed(*handle).has_value(), "mark failed");
    expect(g_live_payloads.load() == 1, "failed payload held");
    expect(submission.cancel().has_value(), "explicit cancel");
    expect(g_destroy_count.load() == destroys_before + 1,
           "failed destroy once");
    expect(!submission.cancel().has_value(), "double cancel rejected");
  }

  // Double retire is idempotent while retiring; after release it is stale.
  {
    auto handle =
        registry->emplace_resource<CountingPayload>(GpuResourceDesc{2, 2}, 8);
    expect(registry->request_retire(*handle).has_value(), "retire once");
    expect(!registry->request_retire(*handle).has_value(),
           "retire after release is stale");
  }

  // Retirement stress: many create/submit/cancel cycles without leaks.
  {
    auto big = GpuResourceRegistry::create(4);
    expect(big.has_value(), "stress registry");
    const int destroys_before = g_destroy_count.load();
    constexpr int kCycles = 64;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
      std::vector<GpuResourceHandle> handles;
      for (std::uint32_t i = 0; i < big->capacity(); ++i) {
        auto handle = big->emplace_resource<CountingPayload>(
            GpuResourceDesc{1, 1}, cycle * 10 + static_cast<int>(i));
        expect(handle.has_value(), "stress create");
        expect(big->begin_upload(*handle).has_value(), "stress upload");
        expect(big->mark_resident(*handle).has_value(), "stress resident");
        handles.push_back(*handle);
      }
      {
        auto submission = big->begin_submission();
        for (GpuResourceHandle handle : handles) {
          expect(submission.retain(handle).has_value(), "stress retain");
          expect(big->request_retire(handle).has_value(), "stress retire");
        }
        if ((cycle % 2) == 0) {
          expect(submission.cancel().has_value(), "stress cancel");
        } else {
          expect(submission.complete().has_value(), "stress complete");
        }
      }
      expect(big->live_count() == 0, "stress drained");
      expect(g_live_payloads.load() == 0, "stress no live payloads");
    }
    expect(g_destroy_count.load() == destroys_before + kCycles * 4,
           "stress destroy count");
  }

  expect(g_live_payloads.load() == 0, "final no live payloads");

  if (failures != 0) {
    std::cerr << failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
