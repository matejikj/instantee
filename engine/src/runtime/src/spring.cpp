#include <instantee/runtime/spring.hpp>

#include <cmath>
#include <string_view>

namespace instantee::runtime {
namespace {

diagnostics::Error err(std::string_view code, std::string_view message) {
  return diagnostics::Error{diagnostics::ExitCode::invalid_input,
                            diagnostics::ErrorKind::invalid_input, code,
                            "runtime", message};
}

// Treat |ζ − 1| below this as critical to avoid cancellation in the
// under/overdamped closed forms.
constexpr double kCriticalEpsilon = 1e-9;

bool finite_params(double angular_frequency, double damping_ratio) noexcept {
  return std::isfinite(angular_frequency) && std::isfinite(damping_ratio);
}

SpringRegime classify(double damping_ratio) noexcept {
  if (damping_ratio < 1.0 - kCriticalEpsilon) {
    return SpringRegime::underdamped;
  }
  if (damping_ratio > 1.0 + kCriticalEpsilon) {
    return SpringRegime::overdamped;
  }
  return SpringRegime::critically_damped;
}

double evaluate_displacement(double omega0, double zeta, double delta0,
                             double velocity0, double t) noexcept {
  if (t == 0.0) {
    return delta0;
  }

  switch (classify(zeta)) {
  case SpringRegime::underdamped: {
    const double wd = omega0 * std::sqrt(1.0 - zeta * zeta);
    const double a = delta0;
    const double b = (velocity0 + zeta * omega0 * delta0) / wd;
    const double decay = std::exp(-zeta * omega0 * t);
    return decay * (a * std::cos(wd * t) + b * std::sin(wd * t));
  }
  case SpringRegime::critically_damped: {
    const double a = delta0;
    const double b = velocity0 + omega0 * delta0;
    const double decay = std::exp(-omega0 * t);
    return decay * (a + b * t);
  }
  case SpringRegime::overdamped: {
    const double root = omega0 * std::sqrt(zeta * zeta - 1.0);
    const double r1 = -zeta * omega0 + root;
    const double r2 = -zeta * omega0 - root;
    // δ = A e^{r1 t} + B e^{r2 t}; δ(0)=δ0, δ'(0)=v0.
    const double a = (velocity0 - r2 * delta0) / (r1 - r2);
    const double b = delta0 - a;
    return a * std::exp(r1 * t) + b * std::exp(r2 * t);
  }
  }
  return delta0;
}

} // namespace

std::expected<SpringPreset, diagnostics::Error>
make_spring_preset(double angular_frequency, double damping_ratio) {
  if (!finite_params(angular_frequency, damping_ratio)) {
    return std::unexpected(
        err("non_finite_spring", "Spring parameters must be finite"));
  }
  if (!(angular_frequency > 0.0)) {
    return std::unexpected(
        err("invalid_angular_frequency",
            "Spring angular frequency must be strictly positive"));
  }
  if (damping_ratio < 0.0) {
    return std::unexpected(
        err("invalid_damping_ratio", "Spring damping ratio must be >= 0"));
  }
  return SpringPreset{.angular_frequency = angular_frequency,
                      .damping_ratio = damping_ratio};
}

SpringRegime spring_regime(const SpringPreset &preset) noexcept {
  return classify(preset.damping_ratio);
}

std::expected<double, diagnostics::Error>
sample_spring(const SpringPreset &preset, double from, double to,
              double initial_velocity, double seconds) noexcept {
  if (!std::isfinite(from) || !std::isfinite(to) ||
      !std::isfinite(initial_velocity) || !std::isfinite(seconds)) {
    return std::unexpected(
        err("non_finite_spring_sample", "Spring sample inputs must be finite"));
  }
  if (seconds < 0.0) {
    return std::unexpected(
        err("negative_time", "Spring sample time must be >= 0"));
  }
  if (!(preset.angular_frequency > 0.0) || preset.damping_ratio < 0.0 ||
      !finite_params(preset.angular_frequency, preset.damping_ratio)) {
    return std::unexpected(
        err("invalid_spring_preset", "Spring preset is not valid"));
  }

  const double delta0 = from - to;
  const double delta =
      evaluate_displacement(preset.angular_frequency, preset.damping_ratio,
                            delta0, initial_velocity, seconds);
  if (!std::isfinite(delta)) {
    return std::unexpected(
        err("non_finite_spring_result", "Spring evaluation was not finite"));
  }
  return to + delta;
}

std::expected<double, diagnostics::Error>
sample_spring_at_frame(const SpringPreset &preset, double from, double to,
                       double initial_velocity, core::FrameIndex frame,
                       core::FrameRate rate) noexcept {
  if (!rate.valid()) {
    return std::unexpected(
        err("invalid_frame_rate", "Frame rate must have non-zero components"));
  }
  const double seconds = static_cast<double>(frame.value()) *
                         static_cast<double>(rate.denominator()) /
                         static_cast<double>(rate.numerator());
  return sample_spring(preset, from, to, initial_velocity, seconds);
}

} // namespace instantee::runtime
