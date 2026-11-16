#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <memory>
#include <type_traits>

namespace instantee::spike::apple::detail {

template <typename Ref> struct CfRelease {
  void operator()(Ref value) const noexcept { CFRelease(value); }
};

// Adopt only a +1 Create/Copy reference. get() borrows; a +0 Get reference
// requires CFRetain before adoption. Empty owners never call the deleter.
template <typename Ref>
using CfOwner = std::unique_ptr<std::remove_pointer_t<Ref>, CfRelease<Ref>>;

} // namespace instantee::spike::apple::detail
