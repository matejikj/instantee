#pragma once

#include <instantee/diagnostics/error.hpp>

#include <exception>
#include <string_view>

namespace instantee::diagnostics {

// Map a caught exception to a typed Error at CLI, worker, or callback edges.
// Never rethrows. bad_alloc becomes out_of_memory; other std::exception values
// become unexpected_exception with the what() text owned by Error. Unknown
// non-standard exceptions use a static message and do not attempt allocation
// beyond the Error strings themselves.
[[nodiscard]] Error map_current_exception(std::string_view phase) noexcept;

[[nodiscard]] Error map_exception(const std::exception &ex,
                                  std::string_view phase) noexcept;

[[nodiscard]] Error map_unknown_exception(std::string_view phase) noexcept;

} // namespace instantee::diagnostics
