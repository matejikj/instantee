#include <instantee/core_model/time.hpp>
#include <instantee/export_apple/pixel_buffer.hpp>
#include <instantee/export_apple/video_writer.hpp>
#include <instantee/export_core/output_transaction.hpp>
#include <instantee/render_metal/device.hpp>
#include <instantee/render_metal/test_pattern.hpp>
#include <instantee/runtime/bounded_queue.hpp>
#include <instantee/runtime/frame_credits.hpp>
#include <instantee/runtime/stop_signals.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <semaphore>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace instantee;
using namespace instantee::spike;
using instantee::diagnostics::Error;
using instantee::diagnostics::ExitCode;
using namespace std::chrono_literals;

void ready() {
  std::puts("READY");
  std::fflush(stdout);
}

void await_stop(std::stop_token stop) {
  std::mutex mutex;
  std::condition_variable_any wake;
  std::unique_lock lock(mutex);
  wake.wait(lock, stop, [] { return false; });
}

int queue_wait(std::string_view mode, std::stop_token stop) {
  auto queue = runtime::BoundedQueue<runtime::FrameCredit>::create(1);
  auto credits = runtime::FrameCreditPool::create(2);
  if (!queue || !credits) {
    return 20;
  }
  if (mode == "full") {
    auto first = credits->acquire();
    if (!first || !queue->push(std::move(*first))) {
      return 21;
    }
  }
  bool cancelled = false;
  std::jthread worker([&] {
    try {
      if (mode == "full") {
        auto pending = credits->acquire(stop);
        if (pending) {
          auto result = queue->push(std::move(*pending), stop);
          cancelled = !result && result.error().code == "cancelled";
        }
      } else {
        auto result = queue->pop(stop);
        cancelled = !result && result.error().code == "cancelled";
      }
    } catch (...) {
      cancelled = false;
    }
  });
  while (mode == "full" ? queue->metrics().blocked_push_count == 0
                        : queue->metrics().blocked_pop_count == 0) {
    std::this_thread::sleep_for(1ms);
  }
  ready();
  await_stop(stop);
  // All waiters see the same stop token. Abort retires pending ownership;
  // join occurs while the queue, admission gate and transaction still live.
  credits->close();
  queue->abort();
  worker.join();
  return cancelled && credits->in_flight() == 0 ? 0 : 22;
}

int credit_wait(std::stop_token stop) {
  auto credits = runtime::FrameCreditPool::create(1);
  if (!credits) {
    return 23;
  }
  auto held = credits->acquire();
  bool cancelled = false;
  std::jthread worker([&] {
    auto result = credits->acquire(stop);
    cancelled = !result && result.error().code == "cancelled";
  });
  while (credits->metrics().blocked_acquire_count == 0) {
    std::this_thread::sleep_for(1ms);
  }
  ready();
  await_stop(stop);
  credits->close();
  worker.join();
  return held && cancelled ? 0 : 24;
}

int late_completion(std::stop_token stop) {
  auto credits = runtime::FrameCreditPool::create(1);
  if (!credits) {
    return 25;
  }
  auto credit = credits->acquire();
  if (!credit) {
    return 26;
  }
  auto context = std::make_shared<runtime::FrameCredit>(std::move(*credit));
  std::weak_ptr<runtime::FrameCredit> observer = context;
  std::binary_semaphore complete{0};
  std::atomic_bool acknowledged{false};
  std::jthread callback([context, &complete, &acknowledged] {
    complete.acquire();
    acknowledged = static_cast<bool>(*context);
  });
  context.reset();
  ready();
  await_stop(stop);
  const bool retained = !observer.expired() && credits->in_flight() == 1;
  complete.release();
  callback.join();
  return retained && acknowledged && observer.expired() &&
                 credits->in_flight() == 0
             ? 0
             : 27;
}

int native_wait(std::string_view mode, export_core::OutputTransaction &tx,
                std::stop_token stop) {
  if (mode == "gpu") {
    auto device = metal::Device::open();
    if (!device) {
      return 30;
    }
    auto renderer = metal::TestPatternRenderer::create(*device);
    if (!renderer) {
      return 31;
    }
    auto lease = renderer->submit(0);
    if (!lease) {
      return 32;
    }
    ready();
    await_stop(stop);
    lease->cancel();
    return lease->take_pixels() ? 33 : 0;
  }
  auto pool = apple::PixelBufferPool::create(
      apple::PixelBufferFormat::media_1080x1920(), 2);
  if (!pool) {
    return 34;
  }
  if (mode == "pool") {
    auto a = pool->acquire();
    auto b = pool->acquire();
    if (!a || !b) {
      return 35;
    }
    bool cancelled = false;
    std::jthread worker([&] {
      @autoreleasepool {
        auto result = pool->acquire(stop);
        cancelled = !result && result.error().code == "cancelled";
      }
    });
    ready();
    await_stop(stop);
    worker.join();
    return cancelled ? 0 : 36;
  }
  auto writer = apple::VideoWriter::create({tx.staging_file()});
  auto buffer = pool->acquire(stop);
  if (!writer || !buffer) {
    return 37;
  }
  std::vector<std::uint8_t> pixels(1080U * 1920U * 4U, 128);
  if (!buffer->fill_bgra8(pixels) ||
      !writer->append(*buffer, core::FrameIndex{0}, stop)) {
    return 38;
  }
  if (mode == "published") {
    if (!writer->finish(stop)) {
      return 39;
    }
    auto timeline = apple::extract_presentation_timeline(tx.staging_file());
    if (!timeline ||
        !apple::validate_presentation_timeline(
            *timeline, core::FrameClock::media_30_1(), core::FrameIndex{1})) {
      return 40;
    }
    auto result = tx.commit(stop);
    if (!result || !result->published || result->post_publish_error) {
      return 41;
    }
  }
  ready();
  if (mode == "finish") {
    // The signal can race native finish completion. Both success and cancelled
    // are valid; all native acknowledgements must precede resource teardown.
    auto result = writer->finish(stop);
    if (!result && result.error().code != "cancelled") {
      return 42;
    }
  }
  await_stop(stop);
  writer->cancel();
  return 0;
}

int run(std::string_view mode, const char *target, std::stop_token stop) {
  auto tx = export_core::OutputTransaction::begin(
      {target, export_core::PublishMode::overwrite,
       export_core::DurabilityClass::process_crash_visible});
  if (!tx) {
    return 10;
  }
  int result = 0;
  if (mode == "full" || mode == "empty" || mode == "credits" ||
      mode == "callback" || mode == "hung") {
    {
      std::ofstream partial(tx->staging_file());
      partial << "incomplete private payload";
    }
    if (mode == "hung") {
      ready();
      // Deliberately ignores stop, modeling an uninterruptible native API.
      std::binary_semaphore never{0};
      never.acquire();
    } else if (mode == "credits") {
      result = credit_wait(stop);
    } else if (mode == "callback") {
      result = late_completion(stop);
    } else {
      result = queue_wait(mode, stop);
    }
  } else {
    result = native_wait(mode, *tx, stop);
  }
  if (result) {
    return result;
  }
  if (!tx->published()) {
    auto denied = tx->commit(stop);
    if (denied || denied.error().code != "cancelled") {
      return 11;
    }
  }
  std::printf("{\"published\":%s,\"cancelled\":true}\n",
              tx->published() ? "true" : "false");
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    return 2;
  }
  try {
    std::stop_source stop;
    auto signals = instantee::runtime::StopSignals::start(stop);
    if (!signals || instantee::runtime::StopSignals::start(stop)) {
      return 3;
    }
    @autoreleasepool {
      return run(argv[1], argv[2], stop.get_token());
    }
  } catch (...) {
    return 4;
  }
}
