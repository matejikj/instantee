#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <type_traits>

namespace instantee::spike::transfer::detail {

template <typename Ref> struct CfRelease {
  void operator()(Ref value) const noexcept { CFRelease(value); }
};

template <typename Ref>
using CfOwner = std::unique_ptr<std::remove_pointer_t<Ref>, CfRelease<Ref>>;

template <typename Ref>
[[nodiscard]] CfOwner<Ref> adopt_cf(Ref value) noexcept {
  return CfOwner<Ref>{value};
}

template <typename Ref>
[[nodiscard]] CfOwner<Ref> retain_cf(Ref value) noexcept {
  if (value != nullptr) {
    CFRetain(value);
  }
  return CfOwner<Ref>{value};
}

} // namespace instantee::spike::transfer::detail
