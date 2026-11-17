#include <instantee/core_model/color.hpp>

#include <string_view>

namespace instantee::core {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "core_model", message};
}

} // namespace

std::expected<LinearRgba, diagnostics::Error>
lerp_linear_rgba(LinearRgba from, LinearRgba to, double t) {
  if (!finite_linear_rgba(from) || !finite_linear_rgba(to) ||
      !std::isfinite(t)) {
    return std::unexpected(
        err("non_finite_color", "Color lerp inputs must be finite"));
  }
  if (t <= 0.0) {
    return from;
  }
  if (t >= 1.0) {
    return to;
  }
  return LinearRgba{from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
                    from.b + (to.b - from.b) * t, from.a + (to.a - from.a) * t};
}

} // namespace instantee::core
