#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <type_traits>

namespace instantee::spike::apple::detail {

template <typename Ref> struct CfRelease {
  void operator()(Ref value) const noexcept { CFRelease(value); }
};

// Adopt only a +1 Create/Copy reference. get() borrows; a +0 Get reference
// requires retain_cf before ownership transfer. Empty owners never call the
// deleter.
template <typename Ref>
using CfOwner = std::unique_ptr<std::remove_pointer_t<Ref>, CfRelease<Ref>>;

template <typename Ref>
[[nodiscard]] CfOwner<Ref> adopt_cf(Ref value) noexcept {
  return CfOwner<Ref>{value};
}

// Retains a borrowed (+0) reference into a +1 owner.
template <typename Ref>
[[nodiscard]] CfOwner<Ref> retain_cf(Ref value) noexcept {
  if (value != nullptr) {
    CFRetain(value);
  }
  return CfOwner<Ref>{value};
}

} // namespace instantee::spike::apple::detail
