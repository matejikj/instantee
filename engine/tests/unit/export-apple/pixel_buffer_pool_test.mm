#include "cf_owner.hpp"

#include <instantee/export_apple/pixel_buffer.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

#if !__has_feature(objc_arc)
#error "Apple adapter tests require ARC"
#endif

namespace {

using instantee::spike::Error;
using instantee::spike::apple::PixelBuffer;
using instantee::spike::apple::PixelBufferFormat;
using instantee::spike::apple::PixelBufferPool;
using instantee::spike::apple::detail::adopt_cf;
using instantee::spike::apple::detail::retain_cf;

struct AcquireResult {
  std::optional<PixelBuffer> buffer;
  std::optional<Error> error;
};

[[nodiscard]] int check_cf_adopt_retain() {
  using Owner = instantee::spike::apple::detail::CfOwner<CFDataRef>;
  std::array<UInt8, 2> bytes{9, 7};
  Owner adopted = adopt_cf(CFDataCreate(kCFAllocatorDefault, bytes.data(), 2));
  if (!adopted) {
    return 1;
  }
  Owner retained = retain_cf(adopted.get());
  if (!retained || retained.get() != adopted.get()) {
    return 2;
  }
  adopted.reset();
  if (CFDataGetLength(retained.get()) != 2) {
    return 3;
  }
  return 0;
}

[[nodiscard]] int check_invalid_create() {
  if (PixelBufferPool::create(PixelBufferFormat{0, 1920}, 2)) {
    return 4;
  }
  if (PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 0)) {
    return 5;
  }
  return 0;
}

[[nodiscard]] int check_seal_and_fill(PixelBufferPool &pool) {
  auto buffer = pool.acquire();
  if (!buffer) {
    return 6;
  }
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(buffer->format().width) *
          static_cast<std::size_t>(buffer->format().height) * 4U,
      0x11);
  if (!buffer->fill_bgra8(pixels)) {
    return 7;
  }
  buffer->seal();
  if (!buffer->sealed() || buffer->fill_bgra8(pixels) ||
      buffer->fill_bgra8(pixels).error().code != "sealed") {
    return 8;
  }
  return 0;
}

[[nodiscard]] int check_delayed_retain_blocks_pool() {
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 1);
  if (!pool) {
    return 9;
  }

  std::optional<PixelBuffer> delayed;
  {
    auto first = pool->acquire();
    if (!first) {
      return 10;
    }
    auto retained = first->retain_copy();
    if (!retained) {
      return 11;
    }
    delayed = std::move(*retained);
  }

  std::atomic_bool started{false};
  std::atomic_bool finished{false};
  AcquireResult third;
  std::jthread waiter{[&](std::stop_token stop) {
    started.store(true, std::memory_order_release);
    auto result = pool->acquire(stop);
    if (result) {
      third.buffer = std::move(*result);
    } else {
      third.error = result.error();
    }
    finished.store(true, std::memory_order_release);
  }};

  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  if (finished.load(std::memory_order_acquire)) {
    return 12;
  }

  delayed.reset();
  for (int i = 0; i < 200 && !finished.load(std::memory_order_acquire); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!finished.load(std::memory_order_acquire) || !third.buffer ||
      third.error.has_value()) {
    return 13;
  }
  return 0;
}

[[nodiscard]] int check_pressure_cancel_and_close() {
  auto pool = PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 2);
  if (!pool || pool->capacity() != 2) {
    return 14;
  }
  auto held0 = pool->acquire();
  auto held1 = pool->acquire();
  if (!held0 || !held1) {
    return 15;
  }

  std::atomic_bool started{false};
  AcquireResult blocked;
  std::jthread waiter{[&](std::stop_token stop) {
    started.store(true, std::memory_order_release);
    auto result = pool->acquire(stop);
    if (result) {
      blocked.buffer = std::move(*result);
    } else {
      blocked.error = result.error();
    }
  }};
  while (!started.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  waiter.request_stop();
  waiter.join();
  if (blocked.buffer || !blocked.error || blocked.error->code != "cancelled") {
    return 16;
  }

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
  const auto timed = pool->acquire({}, deadline);
  if (timed || timed.error().code != "deadline_exceeded") {
    return 17;
  }

  std::atomic_bool close_started{false};
  AcquireResult closed_result;
  std::jthread closer{[&] {
    close_started.store(true, std::memory_order_release);
    auto result = pool->acquire();
    if (result) {
      closed_result.buffer = std::move(*result);
    } else {
      closed_result.error = result.error();
    }
  }};
  while (!close_started.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(15));
  pool->close();
  closer.join();
  if (closed_result.buffer || !closed_result.error ||
      closed_result.error->code != "closed") {
    return 18;
  }
  if (pool->acquire() || pool->acquire().error().code != "closed") {
    return 19;
  }
  return 0;
}

[[nodiscard]] int check_partial_failure_paths(PixelBufferPool &pool) {
  auto buffer = pool.acquire();
  if (!buffer) {
    return 20;
  }
  std::vector<std::uint8_t> wrong(16, 0);
  if (buffer->fill_bgra8(wrong) ||
      buffer->fill_bgra8(wrong).error().code != "size_mismatch") {
    return 21;
  }
  return 0;
}

} // namespace

int main() {
  @autoreleasepool {
    if (const int code = check_cf_adopt_retain(); code != 0) {
      return code;
    }
    if (const int code = check_invalid_create(); code != 0) {
      return code;
    }

    auto pool =
        PixelBufferPool::create(PixelBufferFormat::media_1080x1920(), 2);
    if (!pool) {
      return 30;
    }
    if (const int code = check_seal_and_fill(*pool); code != 0) {
      return code;
    }
    if (const int code = check_partial_failure_paths(*pool); code != 0) {
      return code;
    }
    if (const int code = check_delayed_retain_blocks_pool(); code != 0) {
      return code;
    }
    return check_pressure_cancel_and_close();
  }
}
