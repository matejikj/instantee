#include <instantee/render_core/renderer.hpp>

#include <cmath>
#include <limits>
#include <string_view>

namespace instantee::render_core {
namespace {

[[nodiscard]] diagnostics::Error make_invalid(std::string_view code,
                                              std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "render_core", message};
}

[[nodiscard]] bool is_finite_float(float value) noexcept {
  return std::isfinite(value);
}

} // namespace

std::expected<void, diagnostics::Error> validate_pass(const PassDesc &pass) {
  if (pass.width_px == 0 || pass.height_px == 0) {
    return std::unexpected(make_invalid("invalid_pass_extent",
                                        "pass width and height must be > 0"));
  }
  if (pass.color_target == 0) {
    return std::unexpected(
        make_invalid("unbound_color_target",
                     "pass requires a non-zero color_target handle"));
  }
  if (pass.ops.empty()) {
    return std::unexpected(
        make_invalid("empty_pass", "pass requires at least one draw op"));
  }
  for (const DrawOp &op : pass.ops) {
    switch (op.kind) {
    case DrawOpKind::clear:
      break;
    case DrawOpKind::fill_rect:
      if (!is_finite_float(op.rect.x) || !is_finite_float(op.rect.y) ||
          !is_finite_float(op.rect.width) || !is_finite_float(op.rect.height)) {
        return std::unexpected(make_invalid(
            "non_finite_rect", "fill_rect requires finite coords"));
      }
      if (!is_valid_rect(op.rect)) {
        return std::unexpected(make_invalid(
            "invalid_rect", "fill_rect requires positive width and height"));
      }
      break;
    default:
      return std::unexpected(
          make_invalid("unsupported_draw_op", "unknown DrawOpKind"));
    }
  }
  return {};
}

std::expected<void, diagnostics::Error>
FakeRenderer::submit(const PassDesc &pass) {
  auto valid = validate_pass(pass);
  if (!valid) {
    return valid;
  }
  if (fail_next_) {
    diagnostics::Error error = *fail_next_;
    fail_next_.reset();
    return std::unexpected(std::move(error));
  }
  last_ = pass;
  submitted_passes_.push_back(pass);
  submitted_ = true;
  if (submit_count_ < std::numeric_limits<std::uint64_t>::max()) {
    ++submit_count_;
  }
  return {};
}

} // namespace instantee::render_core
