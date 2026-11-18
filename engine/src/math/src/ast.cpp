#include <instantee/math/ast.hpp>

#include <algorithm>
#include <cstdint>

namespace instantee::math {
namespace {

void count_into(const MathNode &node, std::uint32_t &total) noexcept {
  if (total != UINT32_MAX) {
    ++total;
  }
  for (const MathNode &child : node.children) {
    count_into(child, total);
  }
}

[[nodiscard]] std::uint32_t depth_of(const MathNode &node) noexcept {
  std::uint32_t deepest = 1;
  for (const MathNode &child : node.children) {
    deepest =
        std::max(deepest, static_cast<std::uint32_t>(1 + depth_of(child)));
  }
  return deepest;
}

} // namespace

std::uint32_t count_math_nodes(const MathNode &root) noexcept {
  std::uint32_t total = 0;
  count_into(root, total);
  return total;
}

std::uint32_t math_ast_depth(const MathNode &root) noexcept {
  return depth_of(root);
}

} // namespace instantee::math
