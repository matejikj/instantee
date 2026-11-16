#include <instantee/diagnostics/exception_boundary.hpp>

#include <new>
#include <string>

namespace instantee::diagnostics {

Error map_exception(const std::exception &ex, std::string_view phase) noexcept {
  try {
    if (dynamic_cast<const std::bad_alloc *>(&ex) != nullptr) {
      return Error{ExitCode::out_of_memory, ErrorKind::out_of_memory,
                   "out_of_memory", phase, "Memory allocation failed"};
    }
    return Error{ExitCode::unexpected_exception,
                 ErrorKind::unexpected_exception, "unexpected_exception", phase,
                 ex.what()};
  } catch (...) {
    return map_unknown_exception(phase);
  }
}

Error map_unknown_exception(std::string_view phase) noexcept {
  return Error{ExitCode::unexpected_exception, ErrorKind::unexpected_exception,
               "unexpected_exception", phase, "Unknown exception"};
}

Error map_current_exception(std::string_view phase) noexcept {
  try {
    throw;
  } catch (const std::exception &ex) {
    return map_exception(ex, phase);
  } catch (...) {
    return map_unknown_exception(phase);
  }
}

} // namespace instantee::diagnostics
