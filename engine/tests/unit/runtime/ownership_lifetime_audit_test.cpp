#include <instantee/render_core/resource_registry.hpp>
#include <instantee/render_core/staging_pool.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/compiled_scene.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/render_job.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <utility>

#if defined(__METAL_VERSION__) || defined(MTL_EXTERN) || defined(__OBJC__) ||  \
    defined(COREVIDEO_H)
#error "ownership lifetime audit must remain free of Metal/Apple headers"
#endif

namespace {

using instantee::render_core::GpuResourceDesc;
using instantee::render_core::GpuResourcePayload;
using instantee::render_core::GpuResourceRegistry;
using instantee::render_core::StagingBufferPool;
using instantee::render_core::StagingLease;
using instantee::render_core::StagingPoolConfig;
using instantee::runtime::BoundedQueue;
using instantee::runtime::CompiledSceneDraft;
using instantee::runtime::FrameCredit;
using instantee::runtime::FrameCreditPool;
using instantee::runtime::make_compiled_scene;
using instantee::runtime::RenderJob;
using instantee::runtime::RenderJobPhase;
using instantee::runtime::RuntimeNode;
using instantee::runtime::RuntimeNodeKind;

int failures = 0;
std::atomic<int> g_live_payloads{0};
std::atomic<int> g_destroy_count{0};
std::atomic<int> g_queue_item_live{0};

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

struct CountingPayload final : GpuResourcePayload {
  explicit CountingPayload(int id) : id(id) { ++g_live_payloads; }
  ~CountingPayload() override {
    ++g_destroy_count;
    --g_live_payloads;
  }
  int id = 0;
};

struct ThrowingPayload final : GpuResourcePayload {
  ThrowingPayload() { throw std::bad_alloc(); }
};

struct TrackedItem {
  explicit TrackedItem(int value) : value(value) { ++g_queue_item_live; }
  TrackedItem(TrackedItem &&other) noexcept : value(other.value) {
    other.value = -1;
  }
  TrackedItem &operator=(TrackedItem &&other) noexcept {
    if (this != &other) {
      if (value >= 0) {
        --g_queue_item_live;
      }
      value = other.value;
      other.value = -1;
    }
    return *this;
  }
  ~TrackedItem() {
    if (value >= 0) {
      --g_queue_item_live;
    }
  }
  TrackedItem(const TrackedItem &) = delete;
  TrackedItem &operator=(const TrackedItem &) = delete;
  int value = -1;
};

[[nodiscard]] std::unique_ptr<const instantee::runtime::CompiledScene>
sample_scene() {
  CompiledSceneDraft draft;
  draft.canvas_width = 8;
  draft.canvas_height = 8;
  draft.canvas_color = "rec709-sdr";
  draft.duration_frames = instantee::core::FrameIndex{4};
  draft.seed = 7;
  draft.node_ids = {"n0"};
  RuntimeNode node;
  node.kind = RuntimeNodeKind::geometry;
  node.visible = true;
  draft.nodes = {node};
  auto scene = make_compiled_scene(std::move(draft));
  if (!scene) {
    return nullptr;
  }
  return std::move(*scene);
}

void audit_frame_credits() {
  auto pool = FrameCreditPool::create(2);
  expect(pool.has_value(), "credit pool create");
  if (!pool) {
    return;
  }
  auto a = pool->acquire();
  auto b = pool->acquire();
  expect(a.has_value() && b.has_value(), "acquire K credits");
  expect(pool->in_flight() == 2, "in_flight at capacity");

  const auto past =
      std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
  auto third = pool->acquire({}, past);
  expect(!third.has_value(), "no credit beyond capacity");

  FrameCredit moved{std::move(*a)};
  expect(a->empty(), "moved-from credit empty");
  expect(static_cast<bool>(moved), "moved credit live");
  moved.release();
  moved.release(); // idempotent double-release
  expect(pool->in_flight() == 1, "one credit returned");
  b->release();
  expect(pool->in_flight() == 0, "all credits returned");
}

void audit_bounded_queue_abort() {
  g_queue_item_live = 0;
  auto queue = BoundedQueue<TrackedItem>::create(2);
  expect(queue.has_value(), "queue create");
  if (!queue) {
    return;
  }
  expect(queue->push(TrackedItem{1}).has_value(), "push 1");
  expect(queue->push(TrackedItem{2}).has_value(), "push 2");
  expect(g_queue_item_live.load() == 2, "two live queued items");
  queue->abort();
  expect(g_queue_item_live.load() == 0, "abort destroyed pending once");
  auto popped = queue->pop();
  expect(!popped.has_value(), "pop after abort is cancelled/closed");
}

void audit_registry_partial_init_and_stale() {
  g_live_payloads = 0;
  g_destroy_count = 0;
  auto registry = GpuResourceRegistry::create(2);
  expect(registry.has_value(), "registry create");
  if (!registry) {
    return;
  }

  expect(!registry->create_resource(GpuResourceDesc{2, 2}, nullptr).has_value(),
         "null payload rejected");
  expect(!registry->emplace_resource<ThrowingPayload>(GpuResourceDesc{2, 2})
              .has_value(),
         "throwing payload unpublished");
  expect(registry->live_count() == 0, "no live after partial init");

  auto handle =
      registry->emplace_resource<CountingPayload>(GpuResourceDesc{4, 4}, 11);
  expect(handle.has_value(), "create payload");
  expect(registry->begin_upload(*handle).has_value(), "upload");
  expect(registry->mark_resident(*handle).has_value(), "resident");

  auto submission = registry->begin_submission();
  expect(submission.retain(*handle).has_value(), "retain");
  expect(registry->request_retire(*handle).has_value(), "retire while held");
  expect(g_live_payloads.load() == 1, "payload alive under hold");
  expect(submission.cancel().has_value(), "cancel submission");
  expect(g_live_payloads.load() == 0, "single destroy after cancel");
  expect(!registry->state(*handle).has_value(), "stale handle rejected");
}

void audit_staging_borrow_and_cancel() {
  StagingPoolConfig config;
  config.capacity = 1;
  config.width_px = 2;
  config.height_px = 2;
  auto pool = StagingBufferPool::create(config);
  expect(pool.has_value(), "staging pool");
  if (!pool) {
    return;
  }

  auto lease = pool->try_acquire();
  expect(lease.has_value(), "acquire lease");
  if (!lease) {
    return;
  }
  auto writable = lease->pixels();
  expect(writable.has_value() && writable->size() == 16, "writable borrow");
  if (writable) {
    std::fill(writable->begin(), writable->end(), static_cast<std::uint8_t>(9));
  }
  expect(lease->mark_gpu_busy().has_value(), "gpu busy");
  expect(!lease->pixels().has_value(), "mutable borrow refused while busy");
  auto frozen = lease->frozen_bytes();
  expect(frozen.has_value() && !frozen->empty() && (*frozen)[0] == 9,
         "frozen borrow under hold");
  lease->cancel();
  expect(lease->state() == StagingLease::State::cancelled, "cancelled");
  expect(lease->frozen_bytes().has_value() && (*lease->frozen_bytes())[0] == 9,
         "cancel preserves busy bytes");
  expect(lease->complete_gpu().has_value(),
         "delayed gpu complete after cancel");
  lease->release();

  auto again = pool->try_acquire();
  expect(again.has_value(), "reuse capacity after recycle");
  if (again) {
    auto bytes = again->frozen_bytes();
    expect(bytes.has_value(), "reused lease readable");
    if (bytes) {
      expect(std::all_of(bytes->begin(), bytes->end(),
                         [](std::uint8_t value) { return value == 0; }),
             "no previous-frame leak after recycle");
    }
  }

  auto blocked = pool->try_acquire();
  expect(!blocked.has_value(), "capacity limit without fallback alloc");
  if (!blocked) {
    expect(blocked.error().code == "would_block", "would_block code");
  }
}

void audit_late_callback_after_job_destroy() {
  struct LateContext {
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    std::atomic_bool touched{false};
  };

  auto context = std::make_shared<LateContext>();
  std::weak_ptr<LateContext> weak = context;

  {
    auto scene = sample_scene();
    expect(scene != nullptr, "sample scene");
    if (!scene) {
      return;
    }
    RenderJob job(std::move(scene));
    expect(job.enter(RenderJobPhase::preparing).has_value(), "preparing");
    expect(job.enter(RenderJobPhase::rendering).has_value(), "rendering");
    expect(job.cancel().has_value(), "cancel before publish");
    expect(job.phase() == RenderJobPhase::cancelled, "cancelled phase");
    // Shared context deliberately outlives the job stack frame.
  }

  expect(weak.lock() != nullptr, "shared context outlives job");
  std::thread late([context] {
    context->touched.store(true);
    {
      std::lock_guard lock(context->mutex);
      context->finished = true;
    }
    context->cv.notify_all();
  });
  {
    std::unique_lock lock(context->mutex);
    context->cv.wait_for(lock, std::chrono::seconds(2),
                         [&] { return context->finished; });
  }
  late.join();
  expect(context->touched.load(), "late callback ran");
  expect(context->finished, "late callback completed");
  context.reset();
  expect(weak.lock() == nullptr, "context destroyed after last owner");
}

void audit_native_stress() {
  g_live_payloads = 0;
  g_destroy_count = 0;
  auto registry = GpuResourceRegistry::create(4);
  expect(registry.has_value(), "stress registry");
  if (!registry) {
    return;
  }
  for (int round = 0; round < 64; ++round) {
    auto handle = registry->emplace_resource<CountingPayload>(
        GpuResourceDesc{2, 2}, round);
    expect(handle.has_value(), "stress create");
    if (!handle) {
      return;
    }
    expect(registry->begin_upload(*handle).has_value(), "stress upload");
    expect(registry->mark_resident(*handle).has_value(), "stress resident");
    auto submission = registry->begin_submission();
    expect(submission.retain(*handle).has_value(), "stress retain");
    expect(registry->request_retire(*handle).has_value(), "stress retire");
    if ((round % 2) == 0) {
      expect(submission.complete().has_value(), "stress complete");
    } else {
      expect(submission.cancel().has_value(), "stress cancel");
    }
  }
  expect(g_live_payloads.load() == 0, "stress no live payloads");
  expect(g_destroy_count.load() == 64, "stress single destroy per payload");
}

} // namespace

int main() {
  audit_frame_credits();
  audit_bounded_queue_abort();
  audit_registry_partial_init_and_stale();
  audit_staging_borrow_and_cancel();
  audit_late_callback_after_job_destroy();
  audit_native_stress();

  if (failures != 0) {
    std::cerr << failures << " ownership lifetime assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "ownership_lifetime_audit_test: ok\n";
  return EXIT_SUCCESS;
}
