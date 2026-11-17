#pragma once

#include <Metal/Metal.hpp>

#include <utility>

namespace instantee::render_metal::detail {

// Adopts a +1 Create/Copy Metal or Foundation object. Unlike NS::SharedPtr,
// reset and destruction are no-ops for a null pointer so sanitizers accept
// moved-from and failed-initialization states.
template <typename T> class MetalOwner {
public:
  MetalOwner() noexcept = default;
  explicit MetalOwner(T *value) noexcept : value_(value) {}
  MetalOwner(MetalOwner &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  MetalOwner &operator=(MetalOwner &&other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }
  ~MetalOwner() { reset(); }

  MetalOwner(const MetalOwner &) = delete;
  MetalOwner &operator=(const MetalOwner &) = delete;

  [[nodiscard]] T *get() const noexcept { return value_; }
  [[nodiscard]] T *operator->() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != nullptr;
  }

  void reset() noexcept {
    if (value_ != nullptr) {
      value_->release();
      value_ = nullptr;
    }
  }

  void reset(T *value) noexcept {
    reset();
    value_ = value;
  }

private:
  T *value_ = nullptr;
};

// Retains a non-owning Metal/Foundation pointer into a +1 MetalOwner.
template <typename T>
[[nodiscard]] MetalOwner<T> retain_owner(T *value) noexcept {
  if (value != nullptr) {
    value->retain();
  }
  return MetalOwner<T>{value};
}

} // namespace instantee::render_metal::detail
