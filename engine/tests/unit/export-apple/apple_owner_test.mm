#include "cf_owner.hpp"
#include <instantee/export_apple/workspace.hpp>

#include <array>
#include <type_traits>
#include <utility>

int main() {
  using instantee::spike::apple::detail::CfOwner;
  using Owner = CfOwner<CFDataRef>;
  static_assert(!std::is_copy_constructible_v<Owner>);
  static_assert(!std::is_copy_assignable_v<Owner>);
  static_assert(std::is_nothrow_move_constructible_v<Owner>);
  static_assert(std::is_nothrow_move_assignable_v<Owner>);
  static_assert(std::is_nothrow_destructible_v<Owner>);
  @autoreleasepool {
    Owner empty;
    empty.reset();
    Owner data;
    {
      std::array<UInt8, 3> source{1, 0, 255};
      data.reset(CFDataCreate(kCFAllocatorDefault, source.data(),
                              static_cast<CFIndex>(source.size())));
      source.fill(42);
    }
    if (!data) {
      return 1;
    }
    // An explicit second retain survives retirement of the original owner.
    CFRetain(data.get());
    Owner retained{data.get()};
    Owner moved{std::move(data)};
    if (data || !moved) {
      return 2;
    }
    empty = std::move(moved);
    if (moved || !empty) {
      return 3;
    }
    empty.reset();
    const auto *bytes = CFDataGetBytePtr(retained.get());
    if (CFDataGetLength(retained.get()) != 3 || bytes[0] != 1 ||
        bytes[1] != 0 || bytes[2] != 255) {
      return 4;
    }
    if (!instantee::spike::apple::check_workspace()) {
      return 5;
    }
  }
}
