#pragma once

#include <cstddef>
#include <cstdint>

namespace instantee::scene_ir {

// Hard caps for untrusted Scene IR / authoring JSON before GPU work. Tightening
// later is allowed; raising requires a reviewed contract change. Tests may
// construct a stricter InputLimits instance; production uses the defaults.
struct InputLimits {
  std::size_t max_bytes = 16U * 1024U * 1024U;
  std::uint32_t max_depth = 64U;
  std::uint32_t max_values = 100000U;
  std::size_t max_string_bytes = 100000U;
  std::uint32_t max_array_items = 50000U;
  std::uint32_t max_object_members = 4096U;
};

} // namespace instantee::scene_ir
