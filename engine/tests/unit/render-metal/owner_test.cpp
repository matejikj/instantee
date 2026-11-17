#include "metal_owner.hpp"

#include <type_traits>
#include <utility>

namespace {

struct FakeObject {
  int releases = 0;
  void release() { ++releases; }
};

} // namespace

int main() {
  using Owner = instantee::render_metal::detail::MetalOwner<FakeObject>;
  static_assert(!std::is_copy_constructible_v<Owner>);
  static_assert(!std::is_copy_assignable_v<Owner>);
  static_assert(std::is_nothrow_move_constructible_v<Owner>);
  static_assert(std::is_nothrow_move_assignable_v<Owner>);
  static_assert(std::is_nothrow_destructible_v<Owner>);

  Owner empty;
  empty.reset();
  if (empty) {
    return 1;
  }

  FakeObject first{};
  FakeObject second{};
  {
    Owner owned{&first};
    Owner moved{std::move(owned)};
    // Moved-from Owner is empty by contract; clang-tidy cannot prove that.
    const bool owned_empty =
        owned.get() == nullptr; // NOLINT(bugprone-use-after-move)
    if (!owned_empty || moved.get() != &first) {
      return 2;
    }
    Owner assigned;
    assigned = std::move(moved);
    const bool moved_empty =
        moved.get() == nullptr; // NOLINT(bugprone-use-after-move)
    if (!moved_empty || assigned.get() != &first) {
      return 3;
    }
    assigned = Owner{&second};
    if (first.releases != 1 || assigned.get() != &second) {
      return 4;
    }
  }
  if (second.releases != 1 || first.releases != 1) {
    return 5;
  }
}
