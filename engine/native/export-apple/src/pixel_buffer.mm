#include <instantee/export_apple/pixel_buffer.hpp>

#include "cf_owner.hpp"
#include "pixel_buffer_native.hpp"

#include <CoreVideo/CoreVideo.h>

#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

#if !__has_feature(objc_arc)
#error "The Apple export adapter requires ARC"
#endif

namespace instantee::spike::apple {
namespace {

constexpr const char *kPhase = "pixel_buffer";

[[nodiscard]] detail::CfOwner<CFNumberRef>
make_sint32(std::int32_t value) noexcept {
  return detail::adopt_cf(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value));
}

[[nodiscard]] detail::CfOwner<CFDictionaryRef>
make_pixel_attributes(const PixelBufferFormat &format) noexcept {
  const auto width = make_sint32(static_cast<std::int32_t>(format.width));
  const auto height = make_sint32(static_cast<std::int32_t>(format.height));
  const auto pixel_format =
      make_sint32(static_cast<std::int32_t>(kCVPixelFormatType_32BGRA));
  if (!width || !height || !pixel_format) {
    return {};
  }

  const void *keys[] = {kCVPixelBufferPixelFormatTypeKey,
                        kCVPixelBufferWidthKey, kCVPixelBufferHeightKey};
  const void *values[] = {pixel_format.get(), width.get(), height.get()};
  return detail::adopt_cf(CFDictionaryCreate(kCFAllocatorDefault, keys, values,
                                             3, &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks));
}

[[nodiscard]] detail::CfOwner<CFDictionaryRef>
make_threshold_aux(std::uint32_t capacity) noexcept {
  if (capacity >
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
    return {};
  }
  const auto threshold = make_sint32(static_cast<std::int32_t>(capacity));
  if (!threshold) {
    return {};
  }
  const void *keys[] = {kCVPixelBufferPoolAllocationThresholdKey};
  const void *values[] = {threshold.get()};
  return detail::adopt_cf(CFDictionaryCreate(kCFAllocatorDefault, keys, values,
                                             1, &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks));
}

} // namespace

struct PixelBufferPool::Impl {
  detail::CfOwner<CVPixelBufferPoolRef> pool;
  detail::CfOwner<CFDictionaryRef> threshold_aux;
  PixelBufferFormat format{};
  std::uint32_t capacity = 0;
  std::uint64_t release_epoch = 0;
  bool closed = false;
  std::mutex mutex;
  std::condition_variable_any availability;

  void notify_released() noexcept {
    std::lock_guard lock(mutex);
    ++release_epoch;
    availability.notify_all();
  }
};

struct PixelBuffer::Impl {
  detail::CfOwner<CVPixelBufferRef> buffer;
  std::shared_ptr<PixelBufferPool::Impl> pool;
  PixelBufferFormat format{};
  bool sealed = false;

  ~Impl() {
    buffer.reset();
    if (pool) {
      pool->notify_released();
    }
  }
};

PixelBuffer::PixelBuffer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

PixelBuffer::PixelBuffer(PixelBuffer &&) noexcept = default;
PixelBuffer &PixelBuffer::operator=(PixelBuffer &&) noexcept = default;
PixelBuffer::~PixelBuffer() = default;

namespace detail {

void *PixelBufferAccess::raw_buffer(PixelBuffer &buffer) noexcept {
  return buffer.impl_ && buffer.impl_->buffer
             ? static_cast<void *>(buffer.impl_->buffer.get())
             : nullptr;
}

bool PixelBufferAccess::empty(const PixelBuffer &buffer) noexcept {
  return !buffer.impl_ || !buffer.impl_->buffer;
}

CVPixelBufferRef borrow_cv_pixel_buffer(PixelBuffer &buffer) noexcept {
  return static_cast<CVPixelBufferRef>(PixelBufferAccess::raw_buffer(buffer));
}

bool pixel_buffer_empty(const PixelBuffer &buffer) noexcept {
  return PixelBufferAccess::empty(buffer);
}

} // namespace detail

PixelBufferFormat PixelBuffer::format() const noexcept {
  return impl_ ? impl_->format : PixelBufferFormat{};
}

bool PixelBuffer::sealed() const noexcept { return impl_ && impl_->sealed; }

void PixelBuffer::seal() noexcept {
  if (impl_) {
    impl_->sealed = true;
  }
}

std::expected<void, Error>
PixelBuffer::fill_bgra8(std::span<const std::uint8_t> tightly_packed) {
  if (!impl_ || !impl_->buffer) {
    return std::unexpected(Error{ExitCode::export_failure, "buffer_not_ready",
                                 kPhase, "Pixel buffer is not ready"});
  }
  if (impl_->sealed) {
    return std::unexpected(Error{ExitCode::invalid_input, "sealed", kPhase,
                                 "Sealed pixel buffers cannot be mutated"});
  }

  const auto expected_bytes = static_cast<std::size_t>(impl_->format.width) *
                              static_cast<std::size_t>(impl_->format.height) *
                              4U;
  if (tightly_packed.size() != expected_bytes) {
    return std::unexpected(Error{ExitCode::invalid_input, "size_mismatch",
                                 kPhase,
                                 "BGRA payload size does not match format"});
  }

  if (CVPixelBufferLockBaseAddress(impl_->buffer.get(), 0) !=
      kCVReturnSuccess) {
    return std::unexpected(Error{ExitCode::export_failure, "lock_failed",
                                 kPhase,
                                 "Cannot lock pixel buffer base address"});
  }

  auto *destination = static_cast<std::uint8_t *>(
      CVPixelBufferGetBaseAddress(impl_->buffer.get()));
  const auto bytes_per_row = static_cast<std::size_t>(
      CVPixelBufferGetBytesPerRow(impl_->buffer.get()));
  const auto tight_row = static_cast<std::size_t>(impl_->format.width) * 4U;
  if (destination == nullptr || bytes_per_row < tight_row) {
    CVPixelBufferUnlockBaseAddress(impl_->buffer.get(), 0);
    return std::unexpected(Error{ExitCode::export_failure,
                                 "base_address_failed", kPhase,
                                 "Pixel buffer base address is unavailable"});
  }

  for (std::uint32_t y = 0; y < impl_->format.height; ++y) {
    std::memcpy(destination + static_cast<std::size_t>(y) * bytes_per_row,
                tightly_packed.data() + static_cast<std::size_t>(y) * tight_row,
                tight_row);
  }

  if (CVPixelBufferUnlockBaseAddress(impl_->buffer.get(), 0) !=
      kCVReturnSuccess) {
    return std::unexpected(Error{ExitCode::export_failure, "unlock_failed",
                                 kPhase, "Cannot unlock pixel buffer"});
  }
  return {};
}

std::expected<PixelBuffer, Error> PixelBuffer::retain_copy() const {
  if (!impl_ || !impl_->buffer) {
    return std::unexpected(Error{ExitCode::export_failure, "buffer_not_ready",
                                 kPhase, "Pixel buffer is not ready"});
  }
  auto copy = std::unique_ptr<Impl>(new (std::nothrow) Impl{});
  if (!copy) {
    return std::unexpected(Error{ExitCode::export_failure, "out_of_memory",
                                 kPhase,
                                 "Cannot allocate retained pixel buffer"});
  }
  copy->buffer = detail::retain_cf(impl_->buffer.get());
  copy->pool = impl_->pool;
  copy->format = impl_->format;
  copy->sealed = impl_->sealed;
  return PixelBuffer{std::move(copy)};
}

PixelBufferPool::PixelBufferPool(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

PixelBufferPool::PixelBufferPool(PixelBufferPool &&) noexcept = default;
PixelBufferPool &
PixelBufferPool::operator=(PixelBufferPool &&) noexcept = default;
PixelBufferPool::~PixelBufferPool() { close(); }

std::expected<PixelBufferPool, Error>
PixelBufferPool::create(PixelBufferFormat format, std::uint32_t capacity) {
  if (!format.valid() || capacity == 0U) {
    return std::unexpected(
        Error{ExitCode::invalid_input, "invalid_pool_config", kPhase,
              "Pixel buffer pool format/capacity is invalid"});
  }

  auto attrs = make_pixel_attributes(format);
  auto threshold_aux = make_threshold_aux(capacity);
  if (!attrs || !threshold_aux) {
    return std::unexpected(Error{ExitCode::export_failure, "pool_attr_failed",
                                 kPhase,
                                 "Cannot create pixel buffer pool attributes"});
  }

  CVPixelBufferPoolRef raw_pool = nullptr;
  const CVReturn status = CVPixelBufferPoolCreate(kCFAllocatorDefault, nullptr,
                                                  attrs.get(), &raw_pool);
  detail::CfOwner<CVPixelBufferPoolRef> pool = detail::adopt_cf(raw_pool);
  if (status != kCVReturnSuccess || !pool) {
    return std::unexpected(Error{ExitCode::export_failure, "pool_create_failed",
                                 kPhase, "Cannot create CVPixelBufferPool"});
  }

  auto impl = std::shared_ptr<Impl>(new (std::nothrow) Impl{});
  if (!impl) {
    return std::unexpected(Error{ExitCode::export_failure, "out_of_memory",
                                 kPhase, "Cannot allocate pixel buffer pool"});
  }
  impl->pool = std::move(pool);
  impl->threshold_aux = std::move(threshold_aux);
  impl->format = format;
  impl->capacity = capacity;
  return PixelBufferPool{std::move(impl)};
}

PixelBufferFormat PixelBufferPool::format() const noexcept {
  return impl_ ? impl_->format : PixelBufferFormat{};
}

std::uint32_t PixelBufferPool::capacity() const noexcept {
  return impl_ ? impl_->capacity : 0U;
}

bool PixelBufferPool::closed() const noexcept {
  return !impl_ || impl_->closed;
}

void PixelBufferPool::close() noexcept {
  if (!impl_) {
    return;
  }
  std::lock_guard lock(impl_->mutex);
  impl_->closed = true;
  impl_->availability.notify_all();
}

std::expected<PixelBuffer, Error> PixelBufferPool::acquire(
    std::stop_token stop,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
  if (!impl_ || !impl_->pool || !impl_->threshold_aux) {
    return std::unexpected(Error{ExitCode::export_failure, "pool_not_ready",
                                 kPhase, "Pixel buffer pool is not ready"});
  }

  for (;;) {
    if (stop.stop_requested()) {
      return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                   "Pixel buffer acquire was cancelled"});
    }

    {
      std::unique_lock lock(impl_->mutex);
      if (impl_->closed) {
        return std::unexpected(Error{ExitCode::invalid_input, "closed", kPhase,
                                     "Pixel buffer pool is closed"});
      }
    }

    CVPixelBufferRef raw = nullptr;
    const CVReturn status = CVPixelBufferPoolCreatePixelBufferWithAuxAttributes(
        kCFAllocatorDefault, impl_->pool.get(), impl_->threshold_aux.get(),
        &raw);
    if (status == kCVReturnSuccess && raw != nullptr) {
      auto buffer_impl = std::unique_ptr<PixelBuffer::Impl>(
          new (std::nothrow) PixelBuffer::Impl{});
      if (!buffer_impl) {
        CVPixelBufferRelease(raw);
        impl_->notify_released();
        return std::unexpected(Error{ExitCode::export_failure, "out_of_memory",
                                     kPhase,
                                     "Cannot allocate pixel buffer owner"});
      }
      buffer_impl->buffer = detail::adopt_cf(raw);
      buffer_impl->pool = impl_;
      buffer_impl->format = impl_->format;
      return PixelBuffer{std::move(buffer_impl)};
    }

    if (status != kCVReturnWouldExceedAllocationThreshold) {
      if (raw != nullptr) {
        CVPixelBufferRelease(raw);
      }
      return std::unexpected(Error{ExitCode::export_failure, "acquire_failed",
                                   kPhase,
                                   "CVPixelBufferPool allocation failed"});
    }
    if (raw != nullptr) {
      CVPixelBufferRelease(raw);
    }

    // Allocation threshold reached: wait for a released buffer. Never fall
    // back to unbounded CVPixelBufferCreate.
    std::unique_lock lock(impl_->mutex);
    if (impl_->closed) {
      return std::unexpected(Error{ExitCode::invalid_input, "closed", kPhase,
                                   "Pixel buffer pool is closed"});
    }
    const auto epoch = impl_->release_epoch;
    const auto can_retry = [&] {
      return impl_->closed || stop.stop_requested() ||
             impl_->release_epoch != epoch;
    };
    if (deadline.has_value()) {
      if (!impl_->availability.wait_until(lock, stop, *deadline, can_retry) &&
          !can_retry()) {
        return std::unexpected(Error{ExitCode::invalid_input,
                                     "deadline_exceeded", kPhase,
                                     "Timed out waiting for a pixel buffer"});
      }
    } else {
      impl_->availability.wait(lock, stop, can_retry);
    }
    if (stop.stop_requested()) {
      return std::unexpected(Error{ExitCode::invalid_input, "cancelled", kPhase,
                                   "Pixel buffer acquire was cancelled"});
    }
    if (impl_->closed) {
      return std::unexpected(Error{ExitCode::invalid_input, "closed", kPhase,
                                   "Pixel buffer pool is closed"});
    }
    // A buffer was released or a spurious wake occurred; retry create.
  }
}

} // namespace instantee::spike::apple
